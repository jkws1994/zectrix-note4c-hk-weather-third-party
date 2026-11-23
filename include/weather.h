#pragma once
#include <ArduinoJson.h>
#include "district.h"

struct Observation {
    char stamp[17];  // HKT, YYYY-MM-DD HH:MM
    float temperature;
    int humidity;   // -1 when HKO has no matching measurement
    int icon;
    float rainMax;  // -1 when unavailable or under maintenance
    char rainStart[6], rainEnd[6]; // HKT, HH:MM
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
bool readForecast(JsonVariantConst root, Forecast& out);
const char* condition(int icon);
