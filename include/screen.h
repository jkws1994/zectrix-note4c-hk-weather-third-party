#pragma once
#include "weather.h"
#include "battery.h"
#include <stdint.h>
namespace screen {
constexpr int width = 400, height = 300, stride = width/4;
extern uint8_t frame[height * stride];
void weather(const District& district, const Observation& obs, const Forecast& forecast, const char* today,
             bool oldObs, bool oldForecast, const char* problem, const char* locationLabel = "HKO  /  HONG KONG",
             const Battery& battery = Battery{});
void setupClosed();
void setupPage(const char* ssid, const char* password);
}
