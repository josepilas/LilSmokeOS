#pragma once
#include <algorithm>
#include <cstdint>

namespace lilsmoke::media {
// Native ACKs mean copied to the decoder FIFO, not played by the speakers.
class PlaybackClock {
public:
    void reset() { started_=false;last_=frames_=fraction_=0; }
    uint64_t played(uint64_t delivered,uint64_t milliseconds) const {
        if(delivered<frames_){started_=false;frames_=fraction_=0;}
        if(!started_){if(delivered){started_=true;last_=milliseconds;}return 0;}
        if(milliseconds<last_){last_=milliseconds;return frames_;}
        const uint64_t elapsed=std::min<uint64_t>(milliseconds-last_,60000);
        last_=milliseconds;
        const uint64_t credit=elapsed*44100+fraction_;
        const uint64_t step=credit/1000,available=delivered-frames_;
        // Underrun time cannot be banked and spent on new packets later.
        if(step>=available){frames_=delivered;fraction_=0;}
        else {frames_+=step;fraction_=credit%1000;}
        return frames_;
    }
private:
    mutable bool started_=false;
    mutable uint64_t last_=0,frames_=0,fraction_=0;
};
}
