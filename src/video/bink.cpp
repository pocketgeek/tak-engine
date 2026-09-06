#include "video/bink.h"

#include <algorithm>
#include <cstring>

#ifdef TAK_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

namespace tak::video {

#ifdef TAK_HAVE_FFMPEG

struct BinkVideo::Impl {
    std::vector<uint8_t> data;      // owned .bik bytes, demuxed in place
    size_t pos = 0;

    AVIOContext* avio = nullptr;
    AVFormatContext* fmt = nullptr;
    AVCodecContext* ctx = nullptr;
    SwsContext* sws = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* pkt = nullptr;
    int stream = -1;
    int w = 0, h = 0;
    double fps = 30.0;
    bool eofSent = false;

    // ---- custom in-memory AVIO ------------------------------------------------
    static int readPacket(void* opaque, uint8_t* buf, int bufSize) {
        auto* s = static_cast<Impl*>(opaque);
        size_t remain = s->data.size() - s->pos;
        if (remain == 0) return AVERROR_EOF;
        int n = int(std::min<size_t>(size_t(bufSize), remain));
        std::memcpy(buf, s->data.data() + s->pos, size_t(n));
        s->pos += size_t(n);
        return n;
    }
    static int64_t seekPacket(void* opaque, int64_t offset, int whence) {
        auto* s = static_cast<Impl*>(opaque);
        if (whence & AVSEEK_SIZE) return int64_t(s->data.size());
        whence &= ~AVSEEK_FORCE;
        int64_t base = whence == SEEK_CUR ? int64_t(s->pos)
                     : whence == SEEK_END ? int64_t(s->data.size()) : 0;
        int64_t p = std::clamp<int64_t>(base + offset, 0, int64_t(s->data.size()));
        s->pos = size_t(p);
        return p;
    }

    void teardown() {
        if (sws) { sws_freeContext(sws); sws = nullptr; }
        if (frame) av_frame_free(&frame);
        if (pkt) av_packet_free(&pkt);
        if (ctx) avcodec_free_context(&ctx);
        if (fmt) avformat_close_input(&fmt);   // does NOT free custom pb
        if (avio) { av_freep(&avio->buffer); avio_context_free(&avio); }
        stream = -1; w = h = 0; pos = 0; eofSent = false;
    }
    ~Impl() { teardown(); }
};

BinkVideo::BinkVideo() : d_(std::make_unique<Impl>()) {}
BinkVideo::~BinkVideo() = default;
BinkVideo::BinkVideo(BinkVideo&&) noexcept = default;
BinkVideo& BinkVideo::operator=(BinkVideo&&) noexcept = default;

bool BinkVideo::available() { return true; }

bool BinkVideo::open(std::vector<uint8_t> data) {
    close();
    d_->data = std::move(data);
    d_->pos = 0;
    if (d_->data.empty()) return false;

    constexpr int kBuf = 1 << 15;
    unsigned char* buffer = static_cast<unsigned char*>(av_malloc(kBuf));
    if (!buffer) return false;
    d_->avio = avio_alloc_context(buffer, kBuf, 0, d_.get(),
                                  &Impl::readPacket, nullptr, &Impl::seekPacket);
    if (!d_->avio) { av_free(buffer); return false; }

    d_->fmt = avformat_alloc_context();
    if (!d_->fmt) { close(); return false; }
    d_->fmt->pb = d_->avio;
    d_->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    if (avformat_open_input(&d_->fmt, nullptr, nullptr, nullptr) < 0) {
        // avformat_open_input frees fmt on failure; drop our avio too.
        d_->fmt = nullptr;
        close();
        return false;
    }
    if (avformat_find_stream_info(d_->fmt, nullptr) < 0) { close(); return false; }

    for (unsigned i = 0; i < d_->fmt->nb_streams; ++i)
        if (d_->fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            d_->stream = int(i); break;
        }
    if (d_->stream < 0) { close(); return false; }

    AVStream* st = d_->fmt->streams[d_->stream];
    const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
    if (!dec) { close(); return false; }
    d_->ctx = avcodec_alloc_context3(dec);
    if (!d_->ctx || avcodec_parameters_to_context(d_->ctx, st->codecpar) < 0
        || avcodec_open2(d_->ctx, dec, nullptr) < 0) { close(); return false; }

    d_->w = d_->ctx->width;
    d_->h = d_->ctx->height;
    AVRational r = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    d_->fps = (r.num && r.den) ? double(r.num) / double(r.den) : 30.0;
    d_->frame = av_frame_alloc();
    d_->pkt = av_packet_alloc();
    if (!d_->frame || !d_->pkt || d_->w <= 0 || d_->h <= 0) { close(); return false; }
    return true;
}

bool BinkVideo::isOpen() const { return d_->ctx != nullptr; }
void BinkVideo::close() { d_->teardown(); d_->data.clear(); }
int BinkVideo::width() const { return d_->w; }
int BinkVideo::height() const { return d_->h; }
double BinkVideo::fps() const { return d_->fps; }

bool BinkVideo::nextFrame(std::vector<uint8_t>& rgba) {
    if (!isOpen()) return false;
    for (;;) {
        int r = avcodec_receive_frame(d_->ctx, d_->frame);
        if (r == 0) {
            const int fw = d_->frame->width, fh = d_->frame->height;
            d_->sws = sws_getCachedContext(
                d_->sws, fw, fh, AVPixelFormat(d_->frame->format),
                fw, fh, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!d_->sws) { av_frame_unref(d_->frame); return false; }
            // sws SIMD over-writes past a tightly-packed row when the width isn't
            // aligned (odd door widths like 155/221), so scale into a properly
            // aligned + padded image, then copy the rows out tightly (pitch fw*4).
            uint8_t* dst[4] = {nullptr, nullptr, nullptr, nullptr};
            int dstLines[4] = {0, 0, 0, 0};
            if (av_image_alloc(dst, dstLines, fw, fh, AV_PIX_FMT_RGBA, 32) < 0) {
                av_frame_unref(d_->frame); return false;
            }
            sws_scale(d_->sws, d_->frame->data, d_->frame->linesize, 0, fh, dst, dstLines);
            rgba.assign(size_t(fw) * size_t(fh) * 4, 0);
            for (int y = 0; y < fh; ++y)
                std::memcpy(rgba.data() + size_t(y) * size_t(fw) * 4,
                            dst[0] + size_t(y) * size_t(dstLines[0]), size_t(fw) * 4);
            av_freep(&dst[0]);
            d_->w = fw; d_->h = fh;
            av_frame_unref(d_->frame);
            return true;
        }
        if (r != AVERROR(EAGAIN) && r != AVERROR_EOF) return false;
        if (r == AVERROR_EOF) return false;

        // Need more input: pull the next video packet (or flush at EOF).
        int rr = d_->eofSent ? AVERROR_EOF : av_read_frame(d_->fmt, d_->pkt);
        if (rr == AVERROR_EOF || d_->eofSent) {
            if (!d_->eofSent) { d_->eofSent = true; avcodec_send_packet(d_->ctx, nullptr); }
            continue;   // drain remaining frames, then receive_frame yields EOF
        }
        if (rr < 0) return false;
        if (d_->pkt->stream_index == d_->stream)
            avcodec_send_packet(d_->ctx, d_->pkt);
        av_packet_unref(d_->pkt);
    }
}

void BinkVideo::rewind() {
    if (!isOpen()) return;
    av_seek_frame(d_->fmt, d_->stream, 0, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(d_->ctx);
    d_->eofSent = false;
}

#else  // ---- no FFmpeg: stub so callers compile + fall back to GAF art ---------

struct BinkVideo::Impl {};
BinkVideo::BinkVideo() = default;
BinkVideo::~BinkVideo() = default;
BinkVideo::BinkVideo(BinkVideo&&) noexcept = default;
BinkVideo& BinkVideo::operator=(BinkVideo&&) noexcept = default;
bool BinkVideo::available() { return false; }
bool BinkVideo::open(std::vector<uint8_t>) { return false; }
bool BinkVideo::isOpen() const { return false; }
void BinkVideo::close() {}
int BinkVideo::width() const { return 0; }
int BinkVideo::height() const { return 0; }
double BinkVideo::fps() const { return 0.0; }
bool BinkVideo::nextFrame(std::vector<uint8_t>&) { return false; }
void BinkVideo::rewind() {}

#endif

}  // namespace tak::video
