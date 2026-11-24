#pragma once
#include "weather.h"
#include "battery.h"
#include <stdint.h>
namespace screen {
constexpr int width = 400, height = 300, stride = width/4;
extern uint8_t frame[height * stride];
struct WarningDisplay {
    uint32_t active;
    bool available;
};
void weather(const District& district, const Observation& obs, const Forecast& forecast, const char* today,
             bool oldObs, bool oldForecast, const char* problem, const char* locationLabel = "HKO  /  HONG KONG",
             const Battery& battery = Battery{}, WarningDisplay warnings = {0, true});
void districtMap(const DistrictWeather& weather,int selected,const char* status);
void setLanguage(Language language);
void setupClosed();
void setupPage(const char* ssid, const char* password);
}
