#include "location.h"
#include "location_root.h"
#include "http_body.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <cstring>

int locateUsingWifi() {
    JsonDocument query;
    query["considerIp"]=false;
    query["fallbacks"]["ipf"]=false;
    query["fallbacks"]["lacf"]=false;
    JsonArray aps=query["wifiAccessPoints"].to<JsonArray>();
    const int count=WiFi.scanNetworks(false,false,true,150);
    // Results are strongest first. Send only the BSSID and RSSI of eligible visible networks.
    for (int i=0; i<count && aps.size()<12; ++i) {
        const String name=WiFi.SSID(i);
        const uint8_t* mac=WiFi.BSSID(i);
        if (!mac || (mac[0]&3) || !name.length() || name.endsWith("_nomap")) continue;
        const String id=WiFi.BSSIDstr(i);
        bool duplicate=false;
        for (JsonObjectConst ap:aps) if (ap["macAddress"]==id) duplicate=true;
        if (duplicate) continue;
        JsonObject ap=aps.add<JsonObject>();
        ap["macAddress"]=id;
        ap["signalStrength"]=WiFi.RSSI(i);
    }
    WiFi.scanDelete();
    if (aps.size()<2 || query.overflowed()) return -1;
    String payload;
    if (!serializeJson(query,payload)) return -1;

    WiFiClientSecure tls;
    tls.setCACert(locationRoot);
    tls.setHandshakeTimeout(10);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    http.setUserAgent("NOTE4C-HKWeather/3.0");
    if (!http.begin(tls,"https://api.beacondb.net/v1/geolocate")) return -1;
    http.addHeader("Content-Type","application/json");
    const int code=http.POST(payload);
    HttpBody body(2048);
    const bool ok=code==HTTP_CODE_OK && http.getSize()<=2048 && http.writeToStream(&body)>0;
    http.end();
    if (!ok) return -1;
    JsonDocument fix;
    if (deserializeJson(fix,body.bytes) || !fix["fallback"].isNull() ||
        !fix["location"]["lat"].is<double>() || !fix["location"]["lng"].is<double>() ||
        !fix["accuracy"].is<double>()) return -1;
    return districtAt(fix["location"]["lat"],fix["location"]["lng"],fix["accuracy"]);
}
