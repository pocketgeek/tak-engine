#include "video/stream.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace tak::video {
namespace {
using Clock = std::chrono::steady_clock;
int64_t micros() { return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count(); }
// FFmpeg's protocol errors may contain the URL (including the key). Suppress
// logging only on the streaming worker; Bink diagnostics remain available.
thread_local bool privateStreamLog = false;
void logCallback(void* obj, int level, const char* fmt, va_list args) {
    if (!privateStreamLog) av_log_default_callback(obj, level, fmt, args);
}
void check(int result) { if (result < 0) throw result; }
struct EncodeFailure {};
struct Encoder {
    AVFormatContext* mux = nullptr;
    AVCodecContext *v = nullptr, *a = nullptr;
    AVStream *vs = nullptr, *as = nullptr;
    AVFrame *vf = nullptr, *af = nullptr, *hw = nullptr;
    AVBufferRef *device = nullptr, *pool = nullptr;
    SwsContext* scale = nullptr;
    SwrContext* resample = nullptr;
    AVAudioFifo* fifo = nullptr;
    bool header = false;
    ~Encoder() {
        sws_freeContext(scale); swr_free(&resample);
        av_audio_fifo_free(fifo);
        av_frame_free(&vf); av_frame_free(&af); av_frame_free(&hw);
        avcodec_free_context(&v); avcodec_free_context(&a);
        av_buffer_unref(&pool); av_buffer_unref(&device);
        if (mux) { if (mux->pb) avio_closep(&mux->pb); avformat_free_context(mux); }
    }
};
bool valid(const StreamConfig& c) {
    return ((c.width == 1280 && c.height == 720) || (c.width == 1920 && c.height == 1080)) &&
        (c.fps == 30 || c.fps == 60) && c.bitrateKbps >= 1000 && c.bitrateKbps <= 20000 &&
        (c.encoder.empty() || c.encoder == "libx264" || c.encoder == "h264_nvenc" ||
         c.encoder == "h264_amf" || c.encoder == "h264_qsv" ||
         c.encoder == "h264_vaapi" || c.encoder == "h264_videotoolbox");
}
}
struct Stream::Impl {
#ifdef TAK_STREAM_TESTING
    std::string testCaFile;
#endif
    std::atomic<bool> running{false}, cancel{false};
    std::atomic<int64_t> deadline{0};
    std::atomic<uint64_t> dropped{0};
    mutable std::mutex stateMutex;
    StreamStatus state;
    std::thread worker;
    std::mutex inputMutex;
    std::condition_variable wake;
    std::vector<uint8_t> pixels;
    int sourceW = 0, sourceH = 0;
    bool fresh = false;
    // Fixed two-second ring, stereo S16 at the game's native 11025 Hz.
    std::mutex audioMutex;
    std::array<int16_t, 22050 * 2> pcm{};
    size_t read = 0, count = 0;

    void report(const std::string& s, const std::string& encoder = {}) {
        std::lock_guard lock(stateMutex); state.state = s;
        if (!encoder.empty()) state.encoder = encoder;
    }
    static int interrupted(void* arg) {
        auto& p = *static_cast<Impl*>(arg);
        return p.cancel.load() || (p.deadline.load() && micros() > p.deadline.load());
    }
    void timeout() { deadline = micros() + 5000000; }
    bool openVideo(Encoder& e, const StreamConfig& c, const std::string& name, const char* node = nullptr) {
        avcodec_free_context(&e.v); av_buffer_unref(&e.pool); av_buffer_unref(&e.device);
        auto* codec = avcodec_find_encoder_by_name(name.c_str());
        if (!codec) return false;
        e.v = avcodec_alloc_context3(codec);
        if (!e.v) return false;
        e.v->width = c.width; e.v->height = c.height;
        e.v->time_base = {1, c.fps}; e.v->framerate = {c.fps, 1};
        e.v->bit_rate = int64_t(c.bitrateKbps) * 1000;
        e.v->rc_max_rate = e.v->bit_rate; e.v->rc_buffer_size = int(e.v->bit_rate * 2);
        e.v->gop_size = c.fps * 2; e.v->max_b_frames = 0;
        e.v->thread_count = 4;
        e.v->pix_fmt = name == "libx264" ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_NV12;
        e.v->color_range = AVCOL_RANGE_MPEG; e.v->colorspace = AVCOL_SPC_BT709;
        e.v->color_primaries = AVCOL_PRI_BT709; e.v->color_trc = AVCOL_TRC_BT709;
        e.v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (name == "h264_vaapi") {
            if (av_hwdevice_ctx_create(&e.device, AV_HWDEVICE_TYPE_VAAPI, node, nullptr, 0) < 0) return false;
            e.pool = av_hwframe_ctx_alloc(e.device);
            if (!e.pool) return false;
            auto* f = reinterpret_cast<AVHWFramesContext*>(e.pool->data);
            f->format = AV_PIX_FMT_VAAPI; f->sw_format = AV_PIX_FMT_NV12;
            f->width = c.width; f->height = c.height; f->initial_pool_size = 8;
            if (av_hwframe_ctx_init(e.pool) < 0) return false;
            e.v->hw_frames_ctx = av_buffer_ref(e.pool); e.v->pix_fmt = AV_PIX_FMT_VAAPI;
        }
        AVDictionary* opts = nullptr;
        if (name == "libx264") {
            av_dict_set(&opts, "preset", "veryfast", 0); av_dict_set(&opts, "tune", "zerolatency", 0);
            av_dict_set(&opts, "x264-params", "nal-hrd=cbr:force-cfr=1:scenecut=0", 0);
        } else if (name == "h264_nvenc") {
            av_dict_set(&opts, "preset", "p4", 0); av_dict_set(&opts, "tune", "ll", 0);
            av_dict_set(&opts, "rc", "cbr", 0);
        } else if (name == "h264_amf") {
            av_dict_set(&opts, "usage", "ultralowlatency", 0); av_dict_set(&opts, "rc", "cbr", 0);
        } else if (name == "h264_videotoolbox") {
            av_dict_set(&opts, "realtime", "1", 0); av_dict_set(&opts, "allow_sw", "0", 0);
        }
        int result = avcodec_open2(e.v, codec, &opts); av_dict_free(&opts);
        return result >= 0;
    }
    std::string chooseVideo(Encoder& e, const StreamConfig& c) {
        std::vector<std::string> names;
        if (!c.encoder.empty()) names.push_back(c.encoder);
        else {
#ifdef __APPLE__
            names = {"h264_videotoolbox"};
#elif defined(_WIN32)
            names = {"h264_nvenc", "h264_amf", "h264_qsv"};
#else
            names = {"h264_nvenc", "h264_vaapi"};
#endif
        }
        names.push_back("libx264");
        for (const auto& name : names) {
            if (cancel) throw AVERROR_EXIT;
            if (name == "h264_vaapi") {
                for (int i = 128; i < 144; ++i) {
                    std::string node = "/dev/dri/renderD" + std::to_string(i);
                    if (openVideo(e, c, name, node.c_str())) return name;
                }
            } else if (openVideo(e, c, name)) return name;
        }
        throw AVERROR_ENCODER_NOT_FOUND;
    }
    void open(Encoder& e, const StreamConfig& c, const std::string& dest, bool local) {
        check(avformat_alloc_output_context2(&e.mux, nullptr, "flv", nullptr));
        e.mux->interrupt_callback = local ? AVIOInterruptCB{nullptr,nullptr} : AVIOInterruptCB{interrupted, this};
        const auto name = chooseVideo(e, c);
        report("CONNECTING", name);
        auto* ac = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!ac) throw AVERROR_ENCODER_NOT_FOUND;
        e.a = avcodec_alloc_context3(ac); if (!e.a) throw AVERROR(ENOMEM);
        e.a->sample_rate = 44100; e.a->sample_fmt = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_default(&e.a->ch_layout, 2);
        e.a->bit_rate = 128000; e.a->time_base = {1, 44100}; e.a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        check(avcodec_open2(e.a, ac, nullptr));
        e.vs = avformat_new_stream(e.mux, nullptr); e.as = avformat_new_stream(e.mux, nullptr);
        if (!e.vs || !e.as) throw AVERROR(ENOMEM);
        e.vs->time_base = e.v->time_base; e.as->time_base = e.a->time_base;
        check(avcodec_parameters_from_context(e.vs->codecpar, e.v));
        check(avcodec_parameters_from_context(e.as->codecpar, e.a));
        e.vf = av_frame_alloc(); e.af = av_frame_alloc(); e.hw = av_frame_alloc();
        if (!e.vf || !e.af || !e.hw) throw AVERROR(ENOMEM);
        e.vf->width = c.width; e.vf->height = c.height;
        e.vf->format = e.pool ? AV_PIX_FMT_NV12 : e.v->pix_fmt;
        check(av_frame_get_buffer(e.vf, 32));
        e.af->format = e.a->sample_fmt; e.af->sample_rate = e.a->sample_rate;
        e.af->nb_samples = e.a->frame_size;
        check(av_channel_layout_copy(&e.af->ch_layout, &e.a->ch_layout));
        check(av_frame_get_buffer(e.af, 0));
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        check(swr_alloc_set_opts2(&e.resample, &stereo, AV_SAMPLE_FMT_FLTP, 44100,
                                &stereo, AV_SAMPLE_FMT_S16, 11025, 0, nullptr));
        check(swr_init(e.resample));
        e.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, 4096);
        if (!e.fifo) throw AVERROR(ENOMEM);
        AVDictionary* opts = nullptr;
        av_dict_set(&opts, "rw_timeout", "5000000", 0);
        av_dict_set(&opts, "tls_verify", "1", 0);
#if !defined(_WIN32) && !defined(__APPLE__)
        // Common distro trust bundles; static OpenSSL's build prefix isn't the
        // machine's certificate directory. Never ship a frozen trust bundle.
        for (const char* path : {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"}) {
            if (std::filesystem::exists(path)) { av_dict_set(&opts, "ca_file", path, 0); break; }
        }
#endif
#ifdef TAK_STREAM_TESTING
        if (!testCaFile.empty()) av_dict_set(&opts, "ca_file", testCaFile.c_str(), 0);
#endif
        timeout();
        int result = avio_open2(&e.mux->pb, dest.c_str(), AVIO_FLAG_WRITE, &e.mux->interrupt_callback, &opts);
        av_dict_free(&opts); check(result);
        av_dict_set(&opts, "flvflags", local ? "0" : "no_duration_filesize", 0);
        timeout(); result = avformat_write_header(e.mux, &opts); av_dict_free(&opts); check(result);
        e.header = true; deadline = 0;
    }
    void packets(Encoder& e, AVCodecContext* codec, AVStream* stream, AVFrame* frame) {
        if (avcodec_send_frame(codec, frame) < 0) throw EncodeFailure{};
        AVPacket* packet = av_packet_alloc(); if (!packet) throw AVERROR(ENOMEM);
        int result = 0;
        bool writeFailure = false;
        while ((result = avcodec_receive_packet(codec, packet)) >= 0) {
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int size = packet->size;
            timeout(); result = av_interleaved_write_frame(e.mux, packet); deadline = 0;
            if (result < 0) { writeFailure = true; break; }
            { std::lock_guard lock(stateMutex); state.bytes += size; }
            av_packet_unref(packet);
        }
        av_packet_free(&packet);
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF) {
            if (writeFailure) check(result);
            throw EncodeFailure{};
        }
    }
    void audioFrame(Encoder& e, int64_t pts) {
        while (av_audio_fifo_size(e.fifo) < e.af->nb_samples) {
            std::array<int16_t, 512> input{};
            { std::lock_guard lock(audioMutex);
              for (int i = 0; i < 256 && count; ++i) {
                  input[i*2] = pcm[read*2]; input[i*2+1] = pcm[read*2+1];
                  read = (read + 1) % 22050; --count;
              }
            }
            float left[1152], right[1152];
            uint8_t* out[] = {reinterpret_cast<uint8_t*>(left), reinterpret_cast<uint8_t*>(right)};
            const uint8_t* in[] = {reinterpret_cast<const uint8_t*>(input.data())};
            int n = swr_convert(e.resample, out, 1152, in, 256); check(n);
            check(av_audio_fifo_write(e.fifo, reinterpret_cast<void**>(out), n));
        }
        check(av_frame_make_writable(e.af));
        check(av_audio_fifo_read(e.fifo, reinterpret_cast<void**>(e.af->data), e.af->nb_samples));
        e.af->pts = pts; packets(e, e.a, e.as, e.af);
    }
    void run(StreamConfig c, std::string dest, bool local) {
        privateStreamLog = true;
        int attempts = 0;
        while (!cancel) {
            try {
                Encoder e;
                open(e, c, dest, local);
                { std::lock_guard lock(audioMutex); count = read = 0; }
                report(local ? "RECORDING" : "LIVE");
                int64_t start = micros(), vpts = 0, apts = 0;
                bool haveVideo = false;
                std::vector<uint8_t> frame;
                int width = 0, height = 0;
                while (!cancel) {
                    int64_t elapsed = micros() - start;
                    if (elapsed - vpts * 1000000 / c.fps > 1000000) throw AVERROR(ETIMEDOUT);
                    if (vpts * 1000000 / c.fps <= elapsed) {
                        check(av_frame_make_writable(e.vf));
                        { std::lock_guard lock(inputMutex);
                          if (fresh) { frame.swap(pixels); width = sourceW; height = sourceH; fresh = false; }
                        }
                        if (!frame.empty()) {
                            check(av_frame_make_writable(e.vf));
                            // Letterbox instead of distorting non-16:9 windows.
                            int dw = c.width, dh = int(int64_t(height) * dw / width) & ~1;
                            if (dh > c.height) { dh = c.height; dw = int(int64_t(width) * dh / height) & ~1; }
                            dw = std::max(dw, 2); dh = std::max(dh, 2);
                            e.scale = sws_getCachedContext(e.scale, width, height, AV_PIX_FMT_RGBA,
                                dw, dh, static_cast<AVPixelFormat>(e.vf->format), SWS_BILINEAR, nullptr, nullptr, nullptr);
                            if (!e.scale) throw AVERROR(ENOMEM);
                            const int* coeff = sws_getCoefficients(SWS_CS_ITU709);
                            check(sws_setColorspaceDetails(e.scale, coeff, 1, coeff, 0, 0, 1<<16, 1<<16));
                            ptrdiff_t strides[4] = {e.vf->linesize[0], e.vf->linesize[1], e.vf->linesize[2], e.vf->linesize[3]};
                            check(av_image_fill_black(e.vf->data, strides, static_cast<AVPixelFormat>(e.vf->format), AVCOL_RANGE_MPEG, c.width, c.height));
                            const int x = ((c.width-dw)/2)&~1, y = ((c.height-dh)/2)&~1;
                            uint8_t* out[4] = {e.vf->data[0] + y*e.vf->linesize[0]+x,
                                e.vf->data[1]+y/2*e.vf->linesize[1]+(e.vf->format==AV_PIX_FMT_NV12 ? x : x/2),
                                e.vf->data[2] ? e.vf->data[2]+y/2*e.vf->linesize[2]+x/2 : nullptr, nullptr};
                            const uint8_t* in[] = {frame.data(), nullptr, nullptr, nullptr}; int pitch[] = {width*4,0,0,0};
                            check(sws_scale(e.scale, in, pitch, 0, height, out, e.vf->linesize));
                            haveVideo = true;
                            frame.clear();
                        }
                        if (!haveVideo) { // valid black frame until first capture
                            ptrdiff_t stride[4] = {e.vf->linesize[0],e.vf->linesize[1],e.vf->linesize[2],0};
                            check(av_image_fill_black(e.vf->data, stride, static_cast<AVPixelFormat>(e.vf->format), AVCOL_RANGE_MPEG,c.width,c.height));
                        }
                        e.vf->pts = vpts++;
                        if (e.pool) {
                            av_frame_unref(e.hw); check(av_hwframe_get_buffer(e.pool, e.hw, 0));
                            check(av_hwframe_transfer_data(e.hw, e.vf, 0)); e.hw->pts = e.vf->pts;
                            packets(e, e.v, e.vs, e.hw);
                        } else packets(e, e.v, e.vs, e.vf);
                        { std::lock_guard lock(stateMutex); ++state.frames; }
                    }
                    if (apts * 1000000 / 44100 <= elapsed) {
                        audioFrame(e, apts); apts += e.af->nb_samples;
                    }
                    const int64_t next = std::min(vpts*1000000/c.fps, apts*1000000/44100);
                    std::unique_lock lock(inputMutex);
                    wake.wait_for(lock, std::chrono::microseconds(std::max<int64_t>(0, start+next-micros())), [&]{return cancel.load();});
                }
                if (local) { // files can be finalized even after stop interrupts network
                    deadline = 0; e.mux->interrupt_callback = {nullptr,nullptr};
                    packets(e,e.v,e.vs,nullptr); packets(e,e.a,e.as,nullptr);
                    av_write_trailer(e.mux);
                }
            } catch (EncodeFailure) {
                if (cancel) break;
                if (c.encoder == "libx264") { report("FAILED - ENCODER ERROR"); break; }
                c.encoder = "libx264";
                report("RESTARTING WITH CPU ENCODER");
            } catch (int) {
                if (cancel) break;
                if (local || ++attempts > 3) { report("FAILED - CHECK KEY, CONNECTION OR ENCODER"); break; }
                report("RECONNECTING " + std::to_string(attempts) + "/3");
                std::unique_lock lock(inputMutex);
                wake.wait_for(lock, std::chrono::seconds(1 << attempts), [&]{return cancel.load();});
            } catch (...) { report("FAILED - STREAMING RESOURCE ERROR"); break; }
        }
        std::fill(c.key.begin(), c.key.end(), '\0'); std::fill(dest.begin(),dest.end(),'\0');
        if (cancel) report("OFF");
        running = false;
    }
    bool start(const StreamConfig& c, std::string destination, bool local) {
        if (running || !valid(c)) return false;
        if (worker.joinable()) worker.join();
        { std::lock_guard lock(stateMutex); state = {}; state.state = "STARTING"; }
        { std::lock_guard lock(inputMutex); pixels.clear(); fresh = false; }
        { std::lock_guard lock(audioMutex); read = count = 0; }
        cancel = false; dropped = 0; running = true;
        try { worker = std::thread([this,c,d=std::move(destination),local]() mutable {run(c,std::move(d),local);}); }
        catch (...) { running=false; report("FAILED - CANNOT START WORKER"); return false; }
        return true;
    }
};
Stream::Stream(): p_(std::make_unique<Impl>()) {
    static std::once_flag once;
    std::call_once(once, []{ av_log_set_callback(logCallback); avformat_network_init(); });
}
Stream::~Stream() { stop(); if (p_->worker.joinable()) p_->worker.join(); }
bool Stream::start(const StreamConfig& c) {
    if (c.key.empty() || c.key.size() > 256 || c.key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") != std::string::npos) return false;
    return p_->start(c, "rtmps://a.rtmps.youtube.com:443/live2/" + c.key, false);
}
bool Stream::startRecording(const StreamConfig& c, const std::string& path) {
    if (path.empty() || path.find("://") != std::string::npos) return false;
    return p_->start(c, "file:" + path, true);
}
#ifdef TAK_STREAM_TESTING
bool Stream::startTestEndpoint(const StreamConfig& c, const std::string& url, const std::string& caFile) {
    if (active() || !url.starts_with("rtmps://localhost:")) return false;
    p_->testCaFile = caFile;
    return p_->start(c,url,false);
}
#endif
void Stream::stop() { if (active()) p_->report("STOPPING"); p_->cancel = true; p_->wake.notify_all(); }
bool Stream::active() const { return p_->running; }
StreamStatus Stream::status() const {
    std::lock_guard lock(p_->stateMutex); auto s = p_->state;
    s.active = p_->running; s.dropped = p_->dropped; return s;
}
bool Stream::video(const uint8_t* rgba, int w, int h, int pitch) {
    if (!active() || !rgba || w <= 0 || h <= 0 || w > 8192 || h > 8192 || pitch < w*4) return false;
    std::unique_lock lock(p_->inputMutex, std::try_to_lock);
    if (!lock.owns_lock()) { ++p_->dropped; return false; }
    if (p_->fresh) ++p_->dropped;
    p_->pixels.resize(size_t(w)*h*4);
    for (int y=0;y<h;++y) std::memcpy(p_->pixels.data()+size_t(y)*w*4, rgba+size_t(y)*pitch, size_t(w)*4);
    p_->sourceW=w; p_->sourceH=h; p_->fresh=true; return true;
}
void Stream::audio(const int16_t* samples, int frames, int channels) {
    if (!active() || !samples || channels < 1 || channels > 8) return;
    std::unique_lock lock(p_->audioMutex,std::try_to_lock); if (!lock.owns_lock()) return;
    for (int f=0;f<frames;++f) {
        // Bounded latency: when the upload stalls, retain the newest audio.
        if (p_->count == 22050) { p_->read=(p_->read+1)%22050; --p_->count; }
        const int16_t* in = samples + f*channels;
        float l=in[0], r=channels==1 ? l : in[1];
        float weight=1;
        if (channels>=6) { l+=in[2]*.707f; r+=in[2]*.707f; weight+=.707f; }
        if (channels>=4) { int rear=channels==4 ? 2:4; l+=in[rear]*.707f; r+=in[rear+1]*.707f; weight+=.707f; }
        if (channels==8) {l+=in[6]*.707f;r+=in[7]*.707f;weight+=.707f;}
        size_t pos=(p_->read+p_->count)%22050;
        p_->pcm[pos*2]=int16_t(l/weight); p_->pcm[pos*2+1]=int16_t(r/weight); ++p_->count;
    }
}
}
