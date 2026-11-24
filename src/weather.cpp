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

static bool validTemperature(float temperature) {
    return std::isfinite(temperature) && temperature >= -50 && temperature <= 60;
}

bool readObservation(JsonVariantConst root, const District& district, Observation& out) {
    Observation next{};
    next.temperature = stationValue(root["temperature"], district.station, "C");
    if (!validTemperature(next.temperature) ||
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

bool readDistrictWeather(JsonVariantConst root, DistrictWeather& out) {
    DistrictWeather next{};
    const auto temperature = root["temperature"];
    if (!formatStamp(temperature["recordTime"] | "", next.stamp)) return false;
    next.icon = root["icon"][0] | 0;
    for (int i=0; i<districtCount; ++i) {
        const float value=stationValue(temperature,districts[i].station,"C");
        if (!validTemperature(value)) continue;
        next.temperatures[i]=value;
        next.present |= uint32_t(1)<<i;
    }
    if (!next.present) return false;
    out=next;
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

namespace {
// Display order, with the highest-impact signals first. Bit positions are saved in NVS.
constexpr WarningSignal warnings[] = {
    {"WTMW", "Tsunami Warning", "海嘯警告", WarningIcon::tsunami, SignalColour::neutral},
    {"TC10", "Hurricane Signal No. 10", "十號颶風信號", WarningIcon::ten, SignalColour::neutral},
    {"WRAINB", "Black Rainstorm Warning", "黑色暴雨警告信號", WarningIcon::rain, SignalColour::black},
    {"TC9", "Cyclone Signal No. 9", "九號烈風或暴風風力增強信號", WarningIcon::nine, SignalColour::neutral},
    {"WRAINR", "Red Rainstorm Warning", "紅色暴雨警告信號", WarningIcon::rain, SignalColour::red},
    {"TC8NE", "Cyclone Signal No. 8 NE", "八號東北烈風或暴風信號", WarningIcon::eight, SignalColour::neutral},
    {"TC8SE", "Cyclone Signal No. 8 SE", "八號東南烈風或暴風信號", WarningIcon::eight, SignalColour::neutral},
    {"TC8NW", "Cyclone Signal No. 8 NW", "八號西北烈風或暴風信號", WarningIcon::eight, SignalColour::neutral},
    {"TC8SW", "Cyclone Signal No. 8 SW", "八號西南烈風或暴風信號", WarningIcon::eight, SignalColour::neutral},
    {"WFNTSA", "Flooding in northern N.T.", "新界北部水浸特別報告", WarningIcon::flood, SignalColour::neutral},
    {"WL", "Landslip Warning", "山泥傾瀉警告", WarningIcon::landslip, SignalColour::neutral},
    {"WRAINA", "Amber Rainstorm Warning", "黃色暴雨警告信號", WarningIcon::rain, SignalColour::amber},
    {"WTS", "Thunderstorm Warning", "雷暴警告", WarningIcon::lightning, SignalColour::neutral},
    {"TC3", "Strong Wind Signal No. 3", "三號強風信號", WarningIcon::three, SignalColour::neutral},
    {"WMSGNL", "Strong Monsoon Signal", "強烈季候風信號", WarningIcon::wind, SignalColour::neutral},
    {"TC1", "Standby Signal No. 1", "一號戒備信號", WarningIcon::one, SignalColour::neutral},
    {"WHOT", "Very Hot Weather Warning", "酷熱天氣警告", WarningIcon::heat, SignalColour::neutral},
    {"WCOLD", "Cold Weather Warning", "寒冷天氣警告", WarningIcon::cold, SignalColour::neutral},
    {"WFROST", "Frost Warning", "霜凍警告", WarningIcon::frost, SignalColour::neutral},
    {"WFIRER", "Red Fire Danger Warning", "紅色火災危險警告", WarningIcon::fire, SignalColour::red},
    {"WFIREY", "Yellow Fire Danger Warning", "黃色火災危險警告", WarningIcon::fire, SignalColour::amber},
    {"", "Other HKO Warning", "其他天文台警告", WarningIcon::unknown, SignalColour::neutral},
};
constexpr size_t warningCount = sizeof(warnings) / sizeof(warnings[0]);
static_assert(warningCount <= 32, "Warning mask must fit in uint32_t");
}

bool readWarnings(JsonVariantConst root, uint32_t& active) {
    const auto summary = root.as<JsonObjectConst>();
    if (summary.isNull()) return false;
    uint32_t next = 0;
    for (const auto row : summary) {
        const char* code = row.value()["code"] | "";
        const char* action = row.value()["actionCode"] | "";
        if (strcmp(action, "CANCEL") == 0 || strcmp(code, "CANCEL") == 0) continue;
        if (!code[0] || (strcmp(action, "ISSUE") != 0 && strcmp(action, "REISSUE") != 0 &&
                         strcmp(action, "EXTEND") != 0 && strcmp(action, "UPDATE") != 0)) return false;
        size_t index = 0;
        while (index < warningCount - 1 && strcmp(warnings[index].code, code) != 0) ++index;
        next |= uint32_t(1) << index;
    }
    active = next;
    return true;
}

const WarningSignal* takeWarningSignal(uint32_t& active) {
    for (size_t index = 0; index < warningCount; ++index) {
        const uint32_t bit = uint32_t(1) << index;
        if (active & bit) {
            active &= ~bit;
            return &warnings[index];
        }
    }
    return nullptr;
}

const char* condition(int icon, Language language) {
    struct Label { const char* en; const char* hk; };
    static constexpr Label sun[] = {
        {"Sunny","陽光充沛"}, {"Sunny periods","間有陽光"}, {"Sunny intervals","短暫陽光"},
        {"Sun, few showers","間有陽光幾陣驟雨"}, {"Sun and showers","短暫陽光有驟雨"}
    };
    static constexpr Label cloud[] = {
        {"Cloudy","多雲"}, {"Overcast","密雲"}, {"Light rain","微雨"}, {"Rain","雨"},
        {"Heavy rain","大雨"}, {"Thunderstorms","雷暴"}
    };
    static constexpr Label air[] = {
        {"Windy","大風"}, {"Dry","乾燥"}, {"Humid","潮濕"}, {"Fog","霧"}, {"Mist","薄霧"}, {"Haze","煙霞"}
    };
    static constexpr Label temp[] = {{"Hot","熱"}, {"Warm","暖"}, {"Cool","涼"}, {"Cold","冷"}};
    Label label{"Unavailable","暫無資料"};
    if (icon >= 50 && icon <= 54) label=sun[icon-50];
    else if (icon >= 60 && icon <= 65) label=cloud[icon-60];
    else if (icon >= 70 && icon <= 75) label={"Fine night","天色良好"};
    else if (icon == 76) label={"Mainly cloudy","大致多雲"};
    else if (icon == 77) label={"Mainly fine","天色大致良好"};
    else if (icon >= 80 && icon <= 85) label=air[icon-80];
    else if (icon >= 90 && icon <= 93) label=temp[icon-90];
    return Words{language}(label.en,label.hk);
}
