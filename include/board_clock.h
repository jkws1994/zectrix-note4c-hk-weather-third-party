#pragma once
#include <time.h>

namespace boardClock {
time_t phoneTime(const char* text);
bool setSystem(time_t utc);
time_t readRtc();
bool writeRtc(time_t utc);
}
