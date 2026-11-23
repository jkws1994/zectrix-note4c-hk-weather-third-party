#include "weather.h"
#include <cmath>
#include <cstdio>
#include <cstring>

static bool digits(const char* s, size_t n) {
    for (size_t i = 0; i < n; i++) if (s[i] < '0' || s[i] > '9') return false;
    return true;
}

static bool formatStamp(const char* iso, char (&stamp)[17]) {
    if (strlen(iso) != 25 || strcmp(iso + 19, "+08:00") != 0 ||
        iso[4] != '-' || iso[7] != '-' || iso[10] != 'T' || iso[13] != ':' ||
        !digits(iso, 4) || !digits(iso+5, 2) || !digits(iso+8, 2) ||
        !digits(iso+11, 2) || !digits(iso+14, 2)) return false;
    snprintf(stamp, sizeof(stamp), "%.10s %.5s", iso, iso+11);
    return true;
}

static float stationValue(JsonVariantConst field, const char* place, const char* unit) {
    for (JsonObjectConst item : field["data"].as<JsonArrayConst>()) {
        if (item["place"] == place && item["unit"] == unit &&
            item["value"].is<float>()) return item["value"].as<float>();
    }
    return NAN;
}

bool readObservation(JsonVariantConst root, const District& district, Observation& out) {
    Observation next{};
    next.temperature = stationValue(root["temperature"], district.station, "C");
    if (!std::isfinite(next.temperature) || next.temperature < -50 || next.temperature > 60 ||
        !formatStamp(root["temperature"]["recordTime"] | "", next.stamp)) return false;
    const float humidity = stationValue(root["humidity"], "Hong Kong Observatory", "percent");
    next.humidity = humidity >= 0 && humidity <= 100 &&
        root["temperature"]["recordTime"] == root["humidity"]["recordTime"] ? lroundf(humidity) : -1;
    next.icon = root["icon"][0] | 0;
    next.rainMax = -1;
    const auto rain = root["rainfall"];
    char start[17]{}, end[17]{};
    if (formatStamp(rain["startTime"] | "", start) && formatStamp(rain["endTime"] | "", end)) {
        for (JsonObjectConst row : rain["data"].as<JsonArrayConst>()) {
            if (row["place"] != district.rainfallArea || row["unit"] != "mm" ||
                row["main"] == "TRUE" || row["main"] == true || !row["max"].is<float>()) continue;
            const float mm = row["max"];
            if (!std::isfinite(mm) || mm < 0) continue;
            next.rainMax = mm;
            memcpy(next.rainStart, start+11, 6);
            memcpy(next.rainEnd, end+11, 6);
            break;
        }
    }
    out = next;
    return true;
}

bool readForecast(JsonVariantConst root, Forecast& out) {
    Forecast next{};
    for (JsonObjectConst entry : root["weatherForecast"].as<JsonArrayConst>()) {
        const char* date = entry["forecastDate"] | "";
        const char* week = entry["week"] | "";
        if (strlen(date) != 8 || !digits(date, 8) || strlen(week) < 3 ||
            !entry["forecastMintemp"]["value"].is<float>() || !entry["forecastMaxtemp"]["value"].is<float>() ||
            entry["forecastMintemp"]["unit"] != "C" || entry["forecastMaxtemp"]["unit"] != "C") continue;
        const float low = entry["forecastMintemp"]["value"], high = entry["forecastMaxtemp"]["value"];
        if (!(low >= -50 && high <= 60 && low <= high)) continue;
        Day& day = next.days[next.count++];
        snprintf(day.date, sizeof(day.date), "%s", date);
        snprintf(day.weekday, sizeof(day.weekday), "%.3s", week);
        day.low = low;
        day.high = high;
        day.icon = entry["ForecastIcon"] | 0;
        if (next.count == 3) break;
    }
    if (!next.count) return false;
    out = next;
    return true;
}

const char* condition(int icon) {
    static const char* const sun[] = {"Sunny", "Sunny periods", "Sunny intervals", "Sun, few showers", "Sun and showers"};
    static const char* const cloud[] = {"Cloudy", "Overcast", "Light rain", "Rain", "Heavy rain", "Thunderstorms"};
    static const char* const air[] = {"Windy", "Dry", "Humid", "Fog", "Mist", "Haze"};
    static const char* const temp[] = {"Hot", "Warm", "Cool", "Cold"};
    if (icon >= 50 && icon <= 54) return sun[icon-50];
    if (icon >= 60 && icon <= 65) return cloud[icon-60];
    if (icon >= 70 && icon <= 75) return "Fine night";
    if (icon == 76) return "Mainly cloudy";
    if (icon == 77) return "Mainly fine";
    if (icon >= 80 && icon <= 85) return air[icon-80];
    if (icon >= 90 && icon <= 93) return temp[icon-90];
    return "Unavailable";
}
