#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include "refresh.h"
#include "board_clock.h"
#include <time.h>
#include <algorithm>
#include <utility>
#include <vector>
#include "hko_roots.h"
#include "http_body.h"
#include "location.h"
#include "panel.h"
#include "screen.h"

namespace {
RTC_DATA_ATTR refresh::Feed observationSchedule{}, forecastSchedule{};
RTC_DATA_ATTR time_t lastClockSync=0;
RTC_DATA_ATTR int observationDistrict=-2;
constexpr gpio_num_t frontButton = GPIO_NUM_0;
constexpr gpio_num_t downButton = GPIO_NUM_18;
constexpr gpio_num_t upButton = GPIO_NUM_39;
constexpr uint64_t downWakeBit = 1ULL << downButton;
constexpr size_t maxBody = 32*1024;
RTC_DATA_ATTR bool frameKnown = false;
RTC_DATA_ATTR uint32_t priorHash = 0;
RTC_DATA_ATTR DischargeHistory batteryHistory{};
Preferences settings;

struct Credentials { char ssid[33]; char pass[65]; };
struct Configuration {
    Credentials network;
    uint8_t district; // districtCount selects automatic location; original settings stay compatible.
};

template<class T> void restore(const char* key,T& object) {
    T saved{};
    if (settings.getBytesLength(key)==sizeof(T) &&
        settings.getBytes(key,&saved,sizeof(T))==sizeof(T)) object=saved;
}

template<class T> void storeChanged(const char* key,const T& oldValue,const T& newValue) {
    if (memcmp(&oldValue,&newValue,sizeof(T)) != 0 &&
        settings.putBytes(key,&newValue,sizeof(T)) != sizeof(T)) Serial.println("NVS write failed");
}

void showChanged() {
    uint32_t h=2166136261u;
    for (uint8_t b : screen::frame) h=(h^b)*16777619u;
    if (frameKnown && h==priorHash) return;
    frameKnown=refreshPanel();
    if (frameKnown) priorHash=h;
}

void sleepBoard(bool timed) {
    WiFi.mode(WIFI_OFF);
    // EXT1 can wake the S3 on either active-low RTC input. GPIO39 is awake-only.
    // Keep the internal RTC pull-ups powered while the two inputs are armed.
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH,ESP_PD_OPTION_ON);
    const uint32_t start=millis();
    while ((digitalRead(frontButton)==LOW || digitalRead(downButton)==LOW) &&
           millis()-start<1500) delay(20);
    uint64_t mask=0;
    for (gpio_num_t button : {frontButton,downButton}) {
        if (digitalRead(button)==LOW) continue; // Exclude a stuck key until the next wake.
        rtc_gpio_pullup_en(button);
        rtc_gpio_pulldown_dis(button);
        mask |= 1ULL << button;
    }
    const bool armed=mask && esp_sleep_enable_ext1_wakeup(mask,ESP_EXT1_WAKEUP_ANY_LOW)==ESP_OK;
    if (timed || !armed || mask != ((1ULL << frontButton) | downWakeBit))
        esp_sleep_enable_timer_wakeup(uint64_t(refresh::secondsToWake(time(nullptr)))*1000000);
    gpio_hold_en(GPIO_NUM_17);
    gpio_hold_en(GPIO_NUM_6);
    gpio_deep_sleep_hold_en();
    esp_deep_sleep_start();
}

bool connectStation(const Credentials& credentials) {
    WiFi.begin(credentials.ssid,credentials.pass);
    const uint32_t start=millis();
    while (WiFi.status()!=WL_CONNECTED && millis()-start<16000) delay(100);
    return WiFi.status()==WL_CONNECTED;
}

void rememberBoardTime(time_t utc) {
    // Other firmware may store local time. Only restore an RTC initialized by this program.
    if (boardClock::writeRtc(utc) && !settings.getBool("rtcUTC",false))
        settings.putBool("rtcUTC",true);
}

const char form[] = R"HTML(<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Weather setup · NOTE4C</title>
<style>
*{box-sizing:border-box}body{margin:0;background:#f4f3eb;color:#201f1a;font:16px/1.45 system-ui,sans-serif}
main{width:min(100%,32rem);margin:0 auto;padding:26px 18px 38px}.brand{display:flex;align-items:center;gap:9px;font-weight:700;font-size:14px}
.mark{display:inline-grid;place-items:center;width:30px;height:27px;border:2px solid #201f1a;border-radius:5px;background:#ffda45;font-size:19px}
h1{font-size:clamp(34px,7vw,42px);letter-spacing:-.055em;line-height:1.1;margin:18px 0 8px}p{margin:0 0 14px}.intro,.hint{color:#535248}
form,.done{background:#fffef9;border:1px solid #cdcbbf;border-radius:14px;padding:18px;margin-top:20px}
label{display:block;font-weight:700}.row{display:flex;align-items:center;justify-content:space-between;gap:8px}
select,input,button{font:inherit}select,input{width:100%;min-height:48px;padding:9px 11px;background:white;color:inherit;border:1px solid #87877a;border-radius:7px}
select{margin-top:4px}input{margin-top:6px}.hint{font-size:14px;margin:6px 0 0}.field{margin-top:18px}
.pw{position:relative}.pw input{padding-right:72px}.pw button{position:absolute;right:3px;bottom:2px;min-height:44px;min-width:64px;border:0;background:transparent;font-weight:650;color:inherit}
a{color:inherit}.scan{display:inline-flex;align-items:center;min-height:44px;font-weight:650;font-size:14px;text-underline-offset:3px}
button:focus-visible,a:focus-visible,input:focus-visible,select:focus-visible{outline:3px solid #2861aa;outline-offset:2px}
.location{border-top:1px solid #d6d5ca;padding-top:18px}.primary{width:100%;min-height:50px;margin-top:22px;border-radius:8px;border:1px solid #25251d;background:#ffda45;color:#211f17;font-weight:750;cursor:pointer}
button:disabled{opacity:.55;cursor:default}.status{font-size:14px;margin:8px 0 0}.status:empty{display:none}
.error{padding:10px 12px;background:#fff0e8;border-left:3px solid #ac3326;color:#75251e;margin:0 0 8px}
[hidden]{display:none!important}
</style></head><body><main><div class="brand"><span class="mark" aria-hidden="true">☀</span>ZECTRIX NOTE4C</div>)HTML";

struct SetupNetwork {
    String name;
    int8_t rssi;
};

bool scanSetupNetworks(std::vector<SetupNetwork>& networks) {
    networks.clear();
    const int count=WiFi.scanNetworks(false,false,true,150);
    for (int i=0; i<count; ++i) {
        // The indexed SSID convenience method accepts only an 8-bit index.
        const auto* ap=static_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(i));
        if (!ap || !ap->ssid[0]) continue;
        String name(reinterpret_cast<const char*>(ap->ssid));
        auto entry=std::find_if(networks.begin(),networks.end(),
                                [&](const SetupNetwork& saved) { return saved.name==name; });
        if (entry==networks.end()) networks.push_back({std::move(name),ap->rssi});
        else entry->rssi=std::max(entry->rssi,ap->rssi);
    }
    WiFi.scanDelete();
    std::sort(networks.begin(),networks.end(),
              [](const SetupNetwork& left,const SetupNetwork& right) { return left.rssi>right.rssi; });
    return count>=0;
}

void appendNetworkName(String& html, const String& name) {
    for (unsigned int i=0; i<name.length(); ++i) {
        switch (const char ch=name[i]) {
        case '&': html += "&amp;"; break;
        case '<': html += "&lt;"; break;
        case '>': html += "&gt;"; break;
        default: html += static_cast<unsigned char>(ch)<32 ? '?' : ch;
        }
    }
}

String setupForm(const Configuration& config, const char* sessionToken,
                 const std::vector<SetupNetwork>& networks, bool scanOk,
                 const char* message=nullptr, const String& selected="") {
    String html(form);
    html += "<h1>Weather setup</h1><p class='intro'>Choose Wi-Fi and a district for your weather display.</p>"
            "<form id='settings' method='post' action='/save'>";
    if (message) {
        html += "<p class='error' role='alert'>";
        html += message; // Fixed firmware message.
        html += "</p>";
    }
    html += "<div class='row'><label for='net'>Wi-Fi network</label><a class='scan' id='scan' href='/?scan=1'>Scan again</a></div>"
            "<select id='net' name=\"network\" required>";
    if (config.network.ssid[0]) {
        html += "<option value='keep'";
        if (!selected.length() || selected=="keep") html += " selected";
        html += ">Keep saved Wi-Fi · ";
        appendNetworkName(html,String(config.network.ssid));
        html += "</option>";
    } else {
        html += "<option value='' disabled";
        if (!selected.length()) html += " selected";
        html += ">Choose a network</option>";
    }
    for (size_t i=0; i<networks.size(); ++i) {
        html += "<option value='";
        html += String(i);
        html += "'";
        if (selected==String(i)) html += " selected";
        html += ">";
        appendNetworkName(html,networks[i].name);
        html += "</option>";
    }
    html += "</select><p id='scanState' class='hint' role='status'>";
    if (!scanOk) html += "Wi-Fi scan failed. Tap Scan again to retry.";
    else if (networks.empty()) html += "No visible 2.4 GHz networks found. Move closer to your router and scan again.";
    else {
        html += String(networks.size());
        html += networks.size()==1 ? " network nearby · 2.4 GHz" : " networks nearby · 2.4 GHz";
    }
    html += "</p><div class='field' id='passwordField'><label for='pass'>Wi-Fi password</label><div class='pw'>"
            "<input id='pass' name='pass' type='password' maxlength='64' autocomplete='off' autocapitalize='none' spellcheck='false' aria-describedby='passHelp'>"
            "<button type='button' id='reveal' aria-controls='pass' hidden>Show</button></div>"
            "<noscript>Leave the password blank when keeping saved Wi-Fi.</noscript><p id='passHelp' class='hint'>For a network without a password, leave this blank.</p></div>"
            "<div class='field location'><label for='district'>Weather district</label><select id='district' name='district' required>";
    html += (config.district==districtCount) ? "<option value='auto' selected>" : "<option value='auto'>";
    html += "Auto-detect using Wi-Fi</option>";
    for (int i=0; i<districtCount; ++i) {
        html += "<option value='";
        html += String(i);
        html += (i==config.district) ? "' selected>" : "'>";
        html += districts[i].name;
        html += "</option>";
    }
    html += "</select><p id='autoHelp' class='hint'>Auto-detect shares nearby Wi-Fi identifiers and signal strengths with beaconDB to find your district.</p></div>"
            "<input type='hidden' name='session' value='";
    html += sessionToken;
    html += "'><input type='hidden' name='phoneTime' id='phoneTime'><button";
    if (!config.network.ssid[0] && networks.empty()) html += " disabled";
    html += " id='save' type='submit' class='primary'>Save settings</button><p id='saveState' class='status' role='status'></p></form>";
    html += R"HTML(<script>
const byId=id=>document.getElementById(id);
const net=byId('net'), pass=byId('pass'), reveal=byId('reveal'), save=byId('save'), scan=byId('scan');
let scanning=false;
function sync(){
  const needPassword=!!net.value && net.value!=='keep';
  byId('passwordField').hidden=!needPassword;
  pass.disabled=!needPassword;
  byId('autoHelp').hidden=byId('district').value!=='auto';
  save.disabled=scanning || !net.value;
  save.textContent=net.value==='keep'?'Save settings':'Connect & save';
}
function resetPassword(){
  pass.value=''; pass.type='password'; reveal.textContent='Show';
}
reveal.hidden=false;
reveal.onclick=()=>{
  const show=pass.type==='password';
  pass.type=show?'text':'password'; reveal.textContent=show?'Hide':'Show';
};
net.onchange=()=>{resetPassword();sync()};
byId('district').onchange=sync;
if(window.fetch && window.DOMParser) scan.onclick=async event=>{
  event.preventDefault();
  if(scanning)return;
  const prev=net.selectedOptions[0], keep=net.value==='keep';
  const name=prev && net.value ? prev.textContent : null;
  scanning=true; net.disabled=true; sync(); byId('scanState').textContent='Scanning for nearby networks…';
  try{
    const response=await fetch(scan.href,{cache:'no-store'});
    if(!response.ok)throw Error();
    const next=new DOMParser().parseFromString(await response.text(),'text/html');
    const nextNet=next.getElementById('net');
    const token=next.querySelector('[name=session]');
    if(!nextNet || !token || !token.value)throw Error();
    net.replaceChildren(...Array.from(nextNet.options,option=>option.cloneNode(true)));
    byId('settings').elements.session.value=token.value;
    const match=Array.from(net.options).find(o=>keep?o.value==='keep':o.value && o.value!=='keep' && o.textContent===name);
    if(match)net.value=match.value;
    else {
      net.value=Array.from(net.options).some(o=>o.value==='keep')?'keep':'';
      resetPassword();
    }
    byId('scanState').textContent=next.getElementById('scanState').textContent;
  }catch(error){
    byId('scanState').textContent='Scan interrupted. Stay connected to the NOTE4C and tap Scan again.';
  }finally{scanning=false;net.disabled=false;sync()}
};
byId('settings').onsubmit=()=>{
  byId('phoneTime').value=String(Math.floor(Date.now()/1000));
  save.disabled=true;
  byId('saveState').textContent=net.value==='keep'?'Saving your district…':'Checking Wi-Fi. This can take about 16 seconds…';
};
window.addEventListener('pageshow',()=>{sync();byId('saveState').textContent=''});
sync();
</script></main></body></html>)HTML";
    return html;
}

bool provision(Configuration& config) {
    WiFi.mode(WIFI_AP_STA);
    char ssid[26], key[15];
    snprintf(ssid,sizeof(ssid),"NOTE4C-%06X",unsigned(ESP.getEfuseMac()>>24)&0xffffff);
    snprintf(key,sizeof(key),"HK%08X",unsigned(esp_random()));
    if (!WiFi.softAP(ssid,key)) { Serial.println("Setup AP failed"); return false; }
    screen::setupPage(ssid,key);
    showChanged();
    // The station interface may connect while the setup form is still open.
    WebServer server(WiFi.softAPIP(),80);
    std::vector<SetupNetwork> networks;
    bool scanOk=false;
    char sessionToken[17]{};
    bool saved=false;
    server.on("/",HTTP_GET,[&]() {
        if (!sessionToken[0] || server.hasArg("scan")) {
            scanOk=scanSetupNetworks(networks);
            // A form from a previous scan must never select a different network by index.
            const uint32_t tokenHigh=esp_random(), tokenLow=esp_random();
            snprintf(sessionToken,sizeof(sessionToken),"%08x%08x",unsigned(tokenHigh),unsigned(tokenLow));
        }
        server.sendHeader("Cache-Control","no-store");
        server.send(200,"text/html",setupForm(config,sessionToken,networks,scanOk));
    });
    server.on("/save",HTTP_POST,[&]() {
        server.sendHeader("Cache-Control","no-store");
        if (!sessionToken[0] || server.arg("session")!=sessionToken) {
            server.send(403,"text/html",String(form)+"<h1>Refresh setup</h1><p>The network list has changed. Reopen setup to choose from the latest scan.</p><a href='/'>Return to setup</a></main></body></html>");
            return;
        }
        const uint32_t receivedAt=millis();
        const String choice=server.arg("network"), pass=server.arg("pass");
        Configuration proposed=config;
        const auto retry=[&](int code,const char* message) {
            Configuration displayConfig=config;
            displayConfig.district=proposed.district;
            server.send(code,"text/html",setupForm(displayConfig,sessionToken,networks,scanOk,message,choice));
        };
        const String selection=server.arg("district");
        int index=0;
        while (index < districtCount && selection != String(index)) ++index;
        if (index == districtCount && selection != "auto") {
            retry(400,"Choose a district and try again.");
            return;
        }
        proposed.district=index;
        const bool keep=choice=="keep" && config.network.ssid[0];
        size_t networkIndex=0;
        while (networkIndex<networks.size() && choice!=String(networkIndex)) ++networkIndex;
        if (!keep && networkIndex==networks.size()) {
            retry(400,"Choose a Wi-Fi network from the list below.");
            return;
        }
        if (pass.length()>64 || (keep && pass.length())) {
            retry(400,"Check the Wi-Fi password and try again.");
            return;
        }
        if (!keep) {
            proposed.network={};
            networks[networkIndex].name.toCharArray(proposed.network.ssid,sizeof(proposed.network.ssid));
            pass.toCharArray(proposed.network.pass,sizeof(proposed.network.pass));
            if (!connectStation(proposed.network)) {
                WiFi.disconnect();
                retry(400,"Couldn’t join that network. Re-enter the Wi-Fi password, or choose another network.");
                return;
            }
        }
        if (settings.putBytes("config2",&proposed,sizeof(proposed))!=sizeof(proposed)) {
            if (!keep) WiFi.disconnect();
            retry(500,"Couldn’t save settings on the board. Please try again.");
            return;
        }
        if (time(nullptr)<refresh::validClock) {
            const time_t phone=boardClock::phoneTime(server.arg("phoneTime").c_str());
            const int64_t utc=int64_t(phone)+(millis()-receivedAt)/1000;
            if (phone && utc<=INT32_MAX && boardClock::setSystem(time_t(utc))) rememberBoardTime(time_t(utc));
        }
        config=proposed;
        saved=true;
        server.send(200,"text/html",String(form)+"<h1>Settings saved</h1><div class='done'><p>Your NOTE4C is fetching the weather. The display will refresh when it’s ready.</p>You can close this page and reconnect your phone to your usual Wi-Fi.</div></main></body></html>");
    });
    server.begin();
    const uint32_t start=millis();
    // An Up key held to enter setup must be released before it can close setup.
    bool upReleased=digitalRead(upButton)==HIGH;
    while (!saved && millis()-start<300000) {
        if (digitalRead(upButton)==HIGH) upReleased=true;
        else if (upReleased) {
            delay(30);
            if (digitalRead(upButton)==LOW) break;
        }
        server.handleClient();
        delay(5);
    }
    delay(250);
    server.stop();
    WiFi.softAPdisconnect(true);
    return saved;
}

bool fetch(const char* type,JsonDocument& doc) {
    WiFiClientSecure tls;
    tls.setCACert(hkoRoots);
    tls.setHandshakeTimeout(12);
    HTTPClient http;
    http.setConnectTimeout(10000);
    http.setTimeout(10000);
    String url=String("https://data.weather.gov.hk/weatherAPI/opendata/weather.php?lang=en&dataType=")+type;
    if (!http.begin(tls,url)) return false;
    int code=http.GET();
    HttpBody body(maxBody);
    bool ok=code==HTTP_CODE_OK && http.getSize()<=int(maxBody) && http.writeToStream(&body)>0;
    http.end();
    if (ok) ok=!deserializeJson(doc,body.bytes);
    Serial.printf("HKO %s: HTTP %d, %s\n",type,code,ok ? "OK" : "unavailable");
    return ok;
}

struct LocationCheck {
    uint8_t router[6];
    time_t checked;
    int matchedPlusOne; // Zero denotes an unsuccessful check.
};
RTC_DATA_ATTR LocationCheck lastCheck{};

struct Selection { int index; const char* label; };

Selection selectLocation(const Configuration& config,bool canLocate, bool batteryOnly=false) {
    if (config.district<districtCount) {
        lastCheck={};
        return {config.district,"HKO  /  HONG KONG"};
    }
    int last=settings.getUChar("detected",districtCount);
    if (last>=districtCount) last=-1;
    // Hold only the router fingerprint and the recheck timer in RTC memory.
    // Flash keeps a confirmed district index so failed fixes can be labelled accurately.
    bool confirmed=batteryOnly && lastCheck.matchedPlusOne>0 && last==lastCheck.matchedPlusOne-1;
    const uint8_t* router=canLocate ? WiFi.BSSID() : nullptr;
    if (router) {
        uint8_t current[6];
        memcpy(current,router,sizeof(current));
        const time_t now=time(nullptr);
        const bool sameRouter=memcmp(current,lastCheck.router,sizeof(current))==0;
        const time_t interval=lastCheck.matchedPlusOne ? 3600 : refresh::interval;
        if (sameRouter && now>=lastCheck.checked && now-lastCheck.checked<interval) {
            confirmed=lastCheck.matchedPlusOne>0;
            if (confirmed) last=lastCheck.matchedPlusOne-1;
        } else {
            const int found=locateUsingWifi();
            memcpy(lastCheck.router,current,sizeof(current));
            lastCheck.checked=now;
            lastCheck.matchedPlusOne=found+1;
            if (found>=0) {
                if (found!=last) settings.putUChar("detected",found);
                last=found;
                confirmed=true;
            }
        }
    }
    if (last<0) return {-1,"HKO  /  NO FIX"};
    return {last,confirmed ? "HKO  /  AUTO" : "LAST DISTRICT"};
}

void hkt(time_t t,const char* format,char* dest,size_t len) {
    t+=8*3600;
    struct tm local{};
    gmtime_r(&t,&local);
    strftime(dest,len,format,&local);
}
}

void setup() {
    initBoard();
    for (gpio_num_t button : {frontButton,downButton}) {
        rtc_gpio_deinit(button);
        pinMode(button,INPUT_PULLUP);
    }
    pinMode(upButton,INPUT_PULLUP);
    Serial.begin(115200);
    if (esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_UNDEFINED) frameKnown=false;
    settings.begin("hkweather1",false);
    if (time(nullptr)<refresh::validClock && settings.getBool("rtcUTC",false))
        boardClock::setSystem(boardClock::readRtc());
    Configuration config{};
    restore("config2",config);
    if (!config.network.ssid[0]) restore("network",config.network); // First-release credentials.
    if (config.district > districtCount) config.district=0;
    WiFi.persistent(false);
    const bool downWake=esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT1 &&
                        (esp_sleep_get_ext1_wakeup_status() & downWakeBit);
    const bool setupRequested=downWake || digitalRead(upButton)==LOW;
    if (!config.network.ssid[0] || setupRequested) {
        if (!provision(config) && !config.network.ssid[0]) {
            screen::setupClosed();
            showChanged();
            settings.end();
            sleepBoard(false);
        }
    }
    const bool forced=esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_TIMER || setupRequested;
    const time_t before=time(nullptr);
    const auto planned=refresh::latest(before);
    const bool networkWake=forced || before<refresh::validClock ||
        observationSchedule.checkedSlot!=planned.observation || forecastSchedule.checkedSlot!=planned.forecast;
    bool online=false;
    if (networkWake) {
        WiFi.mode(WIFI_STA);
        online=WiFi.status()==WL_CONNECTED || connectStation(config.network);
        if (online && refresh::clockSyncDue(before,lastClockSync)) {
            configTime(0,0,"stdtime.gov.hk","time.cloudflare.com","pool.ntp.org");
            const uint32_t start=millis();
            const uint32_t timeout=before<refresh::validClock ? 35000 : 3000;
            while (millis()-start<timeout) {
                if (sntp_get_sync_status()==SNTP_SYNC_STATUS_COMPLETED) {
                    lastClockSync=time(nullptr);
                    rememberBoardTime(lastClockSync);
                    break;
                }
                delay(50);
            }
        }
    }
    Observation obs{};
    Forecast forecast{};
    const time_t now=time(nullptr);
    const bool clockReady=now>=refresh::validClock;
    const auto slots=refresh::latest(now);
    const Selection selection=selectLocation(config,online && clockReady,!networkWake);
    const District overview{"Hong Kong","Hong Kong Observatory",""};
    const District& district=selection.index>=0 ? districts[selection.index] : overview;
    char observationKey[12];
    if (selection.index>=0) snprintf(observationKey,sizeof(observationKey),"local2_%u",unsigned(selection.index));
    else strcpy(observationKey,"overview");
    restore(observationKey,obs);
    // Only Yau Tsim Mong uses the same station as the original HQ-only release.
    if (!obs.stamp[0] && selection.index==0) {
        struct LegacyObservation { char stamp[17]; float temperature; int humidity, icon; } legacy{};
        restore("obs1",legacy);
        if (legacy.stamp[0]) {
            memcpy(obs.stamp,legacy.stamp,sizeof(obs.stamp));
            obs.temperature=legacy.temperature; obs.humidity=legacy.humidity;
            obs.icon=legacy.icon; obs.rainMax=-1;
        }
    }
    restore("forecast1",forecast);
    bool gotObs=!forced && observationSchedule.checkedSlot==slots.observation &&
                observationDistrict==selection.index && obs.stamp[0];
    bool gotForecast=!forced && forecastSchedule.checkedSlot==slots.forecast && forecast.count>0;
    if (online && clockReady) {
        JsonDocument doc;
        Observation nextObs{};
        if (!gotObs && fetch("rhrread",doc) && readObservation(doc,district,nextObs)) {
            storeChanged(observationKey,obs,nextObs);
            obs=nextObs;
            gotObs=true;
            observationDistrict=selection.index;
            char expected[17];
            hkt(slots.observation,"%Y-%m-%d %H:%M",expected,sizeof(expected));
            observationSchedule.received(slots.observation,strcmp(obs.stamp,expected)>=0);
        }
        doc.clear();
        Forecast nextForecast{};
        if (!gotForecast && fetch("fnd",doc) && readForecast(doc,nextForecast)) {
            storeChanged("forecast1",forecast,nextForecast);
            forecast=nextForecast;
            gotForecast=true;
            char expected[26];
            hkt(slots.forecast,"%Y-%m-%dT%H:%M:00+08:00",expected,sizeof(expected));
            const char* issue=doc["updateTime"] | "";
            forecastSchedule.received(slots.forecast,strcmp(issue,expected)>=0);
        }
    }
    if (!gotObs) observationSchedule.checkedSlot=0;
    if (!gotForecast) forecastSchedule.checkedSlot=0;
    WiFi.mode(WIFI_OFF);
    char today[9]{}, cutoff[17]{};
    if (clockReady) {
        hkt(time(nullptr),"%Y%m%d",today,sizeof(today));
        hkt(time(nullptr)-2*3600,"%Y-%m-%d %H:%M",cutoff,sizeof(cutoff));
    }
    const bool oldObs=!gotObs || strcmp(obs.stamp,cutoff)<0;
    const char* problem="";
    if (networkWake && !online) problem="WI-FI OFFLINE";
    else if (!clockReady) problem="SET CLOCK IN SETUP";
    else if (!gotObs || !gotForecast) problem="UPDATE INCOMPLETE";
    else if (oldObs) problem="OLDER READING";
    Battery battery=readBattery();
    battery.chargeAt=estimateChargeTime(batteryHistory,battery,clockReady ? time(nullptr) : 0);
    screen::weather(district,obs,forecast,today,oldObs,!gotForecast,problem,selection.label,battery);
    showChanged();
    settings.end();
    sleepBoard(true);
}

void loop() {}
