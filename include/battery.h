#pragma once
#include <time.h>

enum class ChargeState { unplugged, charging, full, unknown };

struct Battery {
    int percent = -1;
    ChargeState state = ChargeState::unknown;
    time_t chargeAt = 0; // Estimated time at the reminder level, UTC.
};
constexpr int chargeReminder = 20;

struct DischargeHistory {
    time_t start, last;
    int initialPercent, lastPercent;
};

Battery readBattery();
time_t estimateChargeTime(DischargeHistory& history, const Battery& battery, time_t now);
