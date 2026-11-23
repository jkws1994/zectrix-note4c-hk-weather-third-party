#pragma once

struct District {
    const char* name;         // Dashboard and setup label
    const char* station;      // HKO temperature.data[].place
    const char* rainfallArea; // HKO rainfall.data[].place
};

// Indices are persisted in settings and observation cache keys. Append new entries.
constexpr int districtCount = 18;
extern const District districts[districtCount];
