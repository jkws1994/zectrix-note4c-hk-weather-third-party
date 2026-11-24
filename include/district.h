#pragma once
#include <stdint.h>

struct District {
    const char* name;         // Dashboard and setup label
    const char* station;      // HKO temperature.data[].place
    const char* rainfallArea; // HKO rainfall.data[].place
    const char* nameHK;
    const char* stationHK;
};

// Indices are persisted in settings and observation cache keys. Append new entries.
constexpr int districtCount = 18;
extern const District districts[districtCount];

struct DistrictPoint { int32_t x, y; }; // Longitude and latitude in WGS84 microdegrees.
struct DistrictBoundary {
    const DistrictPoint* points;
    unsigned count;
};
DistrictBoundary districtBoundary(int index);
