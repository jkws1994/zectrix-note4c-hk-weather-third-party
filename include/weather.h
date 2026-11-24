#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include "district.h"
#include "language.h"

struct Observation {
    char stamp[17];  // HKT, YYYY-MM-DD HH:MM
    float temperature;
    int humidity;   // -1 when HKO has no matching measurement
    int icon;
    float rainMax;  // -1 when unavailable or under maintenance
    char rainStart[6], rainEnd[6]; // HKT, HH:MM
};

struct DistrictWeather {
    char stamp[17]; // Shared temperature measurement time in HKT.
    float temperatures[districtCount];
    uint32_t present;
    int icon;
};

struct Day {
    char date[9];   // YYYYMMDD, also used to discard expired rows
    char weekday[4];
    float low, high;
    int icon;
};

struct Forecast {
    Day days[3];
    int count;
};

bool readObservation(JsonVariantConst root, const District& district, Observation& out);
bool readDistrictWeather(JsonVariantConst root, DistrictWeather& out);
bool readForecast(JsonVariantConst root, Forecast& out);
bool readWarnings(JsonVariantConst root, uint32_t& active);
enum class WarningIcon { tsunami, ten, rain, nine, eight, flood, landslip, lightning, three, wind, one, heat, cold, frost, fire, unknown };
enum class SignalColour { neutral, amber, red, black };
struct WarningSignal {
    const char* code;
    const char* label;
    const char* labelHK;
    WarningIcon icon;
    SignalColour colour;
};
const WarningSignal* takeWarningSignal(uint32_t& active);
inline const char* takeWarning(uint32_t& active) {
    const auto* signal=takeWarningSignal(active);
    return signal ? signal->label : nullptr;
}
const char* condition(int icon, Language language = Language::english);
