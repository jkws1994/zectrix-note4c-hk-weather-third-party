#pragma once

#include <stdint.h>
#include <time.h>

namespace refresh {
constexpr uint32_t interval = 15*60;
constexpr uint32_t wakeOffset = 5*60; // Wake at :05, :20, :35 and :50.
constexpr time_t validClock = 1704067200;  // 2024-01-01 UTC

// Allow a timer wake a few seconds early without skipping its work or waking twice.
constexpr int tolerance = 3;
inline uint32_t secondsToWake(time_t now) {
    if (now < validClock) return interval;
    const int phase=(int64_t(now)+tolerance+interval-wakeOffset)%interval;
    return interval-phase+tolerance;
}

// UTC timestamp of the latest scheduled forecast issue, allowing five minutes to publish.
inline int64_t forecastSlot(time_t now) {
    const int64_t eligible=int64_t(now)+tolerance-5*60;
    constexpr int day=86400, hkt=8*3600;
    const int64_t midnight=(eligible+hkt)/day*day-hkt;
    const int64_t morning=midnight+11*3600+30*60;
    const int64_t afternoon=midnight+16*3600+30*60;
    return eligible>=afternoon ? afternoon : (eligible>=morning ? morning : afternoon-day);
}

struct Feed {
    int64_t checkedSlot;
    int64_t delayedSlot;

    // Transport failures retry at the next wake. A valid but late bulletin gets one recheck.
    void received(int64_t slot, bool current) {
        checkedSlot=(current || delayedSlot==slot) ? slot : 0;
        delayedSlot=current ? 0 : slot;
    }
};

inline bool clockSyncDue(time_t now, time_t lastSync) {
    return now<validClock || lastSync<validClock || now<lastSync || now-lastSync>=6*3600;
}
}
