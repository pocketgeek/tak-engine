#pragma once
// The streaming panel's "UPLOAD CAN'T KEEP UP" warning (client/streaming.cpp), for any
// destination. Fed each RATE sample (about one a second) of a LIVE stream: the rate
// actually being sent, the bitrate set, and the DROPPED count. It warns when the rate
// stays under half the set bitrate for five samples in a row, or DROPPED rises in three
// of the last five, and clears after five samples with neither. The first samples of a
// connection are ignored while the encoder and the connection settle.
#include <bit>
#include <cstdint>

namespace tak {

class UploadWatch {
public:
    void reset() { *this = {}; }
    // Returns whether the warning is on after this sample.
    bool sample(uint64_t rateKbps, int setKbps, uint64_t dropped) {
        const bool rose = seen_ && dropped > dropped_;
        dropped_ = dropped; seen_ = true;
        if (settle_ > 0) { --settle_; return warn_; }
        low_ = (low_ << 1 | (rateKbps * 2 < uint64_t(setKbps > 0 ? setKbps : 0))) & 31u;
        rose_ = (rose_ << 1 | rose) & 31u;
        if (low_ == 31u || std::popcount(rose_) >= 3) warn_ = true;
        else if (!low_ && !rose_) warn_ = false;
        return warn_;
    }
    bool warning() const { return warn_; }
private:
    unsigned low_ = 0, rose_ = 0; // the last five samples, newest in bit 0
    uint64_t dropped_ = 0;
    bool seen_ = false, warn_ = false;
    int settle_ = 3;
};

}
