#include "screen.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace screen {
uint8_t frame[height * stride];
enum Ink { black, white, yellow, red };
struct Glyph { unsigned char code; unsigned short offset; signed char left, top; unsigned char w, h, advance; };
struct Font { const Glyph* glyphs; const unsigned char* bits; int count; };
#include "font_data.inc"

static void dot(int x, int y, Ink ink) {
    if (x < 0 || x >= width || y < 0 || y >= height) return;
    uint8_t& value = frame[y*stride + x/4];
    const int shift = 6-2*(x%4);
    value = (value & ~(3 << shift)) | (ink << shift);
}

static void box(int x, int y, int w, int h, Ink ink) {
    for (int j = y; j < y+h; ++j)
        for (int i = x; i < x+w; ++i) dot(i,j,ink);
}

static const Glyph* glyph(const Font& f, char c) {
    for (int i = 0; i < f.count; ++i) if (f.glyphs[i].code == c) return f.glyphs+i;
    return nullptr;
}

static int measure(const Font& f, const char* s) {
    int width = 0;
    while (*s) { const Glyph* g = glyph(f, *s++); if (g) width += g->advance; }
    return width;
}

static int text(int x, int baseline, const Font& f, const char* s, Ink ink = black) {
    while (*s) {
        const Glyph* g = glyph(f, *s++);
        if (!g) continue;
        const uint8_t* data = f.bits + g->offset;
        for (int y = 0; y < g->h; ++y)
            for (int i = 0; i < g->w; ++i)
                if (data[y*((g->w+7)/8) + i/8] & (0x80 >> (i%8)))
                    dot(x+g->left+i, baseline+g->top+y, ink);
        x += g->advance;
    }
    return x;
}

static void number(char* buf, size_t size, float value) {
    snprintf(buf, size, fabsf(value-roundf(value)) < 0.05f ? "%.0f" : "%.1f", value);
}

static void circle(int cx, int cy, int radius, Ink ink) {
    for (int y=-radius; y<=radius; ++y)
        for (int x=-radius; x<=radius; ++x)
            if (x*x+y*y <= radius*radius) dot(cx+x,cy+y,ink);
}

#include "poster_icons.inc"

static const uint8_t* symbol(int code) {
    if (code==50) return sunIcon;
    if (code==51 || code==52) return sunCloudIcon;
    if (code==53 || code==54) return showersIcon;
    if (code==60 || code==61) return cloudIcon;
    if (code>=62 && code<=64) return rainIcon;
    if (code==65) return stormIcon;
    if (code>=70 && code<=75) return nightIcon;
    if (code==76 || code==77) return nightCloudIcon;
    if (code==80) return windIcon;
    if (code==81) return dryIcon;
    if (code==82) return humidIcon;
    if (code>=83 && code<=85) return mistIcon;
    if (code>=90 && code<=93) return thermalIcon;
    return nullptr;
}

static void rightText(int right, int baseline, const Font& font, const char* label, Ink ink=black) {
    text(right-measure(font,label),baseline,font,label,ink);
}

static const char* monthName(const char* digits) {
    static const char* const months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    int m=(digits[0]-'0')*10+digits[1]-'0';
    return (m>=1 && m<=12) ? months[m-1] : "";
}

static void forecastPoster(const Forecast& forecast, const char* today, bool saved) {
    box(0,178,width,24,black);
    text(12,195,small,saved && forecast.count ? "HONG KONG  /  SAVED FORECAST" : "HONG KONG  /  THREE-DAY FORECAST",white);
    rightText(388,195,small,"LOW / HIGH",white);
    int column=0;
    for (int i=0; i<forecast.count && i<3; ++i) {
        const Day& day=forecast.days[i];
        if (today[0] && strcmp(day.date,today)<0) continue;
        int x=12+column*132;
        if (column++) box(x-8,209,1,60,black);
        char date[20], lo[12], hi[12], range[28];
        snprintf(date,sizeof(date),"%.3s  %.2s %s",day.weekday,day.date+6,monthName(day.date+4));
        for (char* c=date; *c; ++c) if (*c>='a' && *c<='z') *c-=32;
        text(x,221,small,date);
        number(lo,sizeof(lo),day.low);
        number(hi,sizeof(hi),day.high);
        snprintf(range,sizeof(range),"%s / %s",lo,hi);
        const Font& size=measure(medium,range)<=113 ? medium : small;
        int end=text(x,248,size,range,red);
        const int degreeY=&size==&medium ? 231 : 238;
        circle(end+3,degreeY,2,red);
        dot(end+3,degreeY,white);
        const char* outlook=condition(day.icon);
        text(x,270,measure(caption,outlook)<=118 ? caption : small,outlook);
    }
    if (!column) text(12,244,body,"Forecast unavailable");
}

constexpr int batteryIconWidth=27, batteryIconGap=5;

static void drawBatteryIcon(int x, int y, int percent) {
    constexpr int bodyWidth=batteryIconWidth-3, height=13, fillWidth=bodyWidth-6;
    box(x,y,bodyWidth,height,black);
    box(x+2,y+2,bodyWidth-4,height-4,yellow);
    box(x+bodyWidth,y+4,3,5,black);
    if (percent>0) {
        const int fill=std::max(1,(fillWidth*percent+50)/100);
        box(x+3,y+3,fill,height-6,black);
    }
}

static void batteryHeader(const Battery& battery, char (&level)[20], char (&detail)[32]) {
    if (battery.percent<0) strcpy(level,"--");
    else snprintf(level,sizeof(level),"%d%%",battery.percent);
    if (battery.state==ChargeState::charging) strcpy(detail,"Charging");
    else if (battery.state==ChargeState::full && battery.percent>=0) strcpy(detail,"Fully charged");
    else if (battery.percent>=0 && battery.percent<=chargeReminder) strcpy(detail,"Charge now");
    else if (battery.chargeAt) {
        time_t local=battery.chargeAt+8*3600;
        struct tm hkt{};
        gmtime_r(&local,&hkt);
        strftime(detail,sizeof(detail),"Charge ~%d %b %H:%M",&hkt);
    }
}

void weather(const District& district, const Observation& obs, const Forecast& forecast, const char* today,
             bool oldObs, bool oldForecast, const char* problem, const char* locationLabel, const Battery& battery) {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,149,yellow);
    char level[20]{}, detail[32]{};
    batteryHeader(battery,level,detail);
    const int levelWidth=batteryIconWidth+batteryIconGap+measure(caption,level);
    const int infoWidth=std::max(levelWidth,measure(small,detail));
    const int titleSpace=width-32-infoWidth;
    const Font& heading=measure(title,district.name)<=titleSpace ? title :
                        (measure(medium,district.name)<=titleSpace ? medium : body);
    text(12,33,heading,district.name);
    const int baseline=detail[0] ? 19 : 28;
    drawBatteryIcon(388-levelWidth,baseline-13,battery.percent);
    rightText(388,baseline,caption,level);
    if (detail[0]) {
        const bool low=battery.state==ChargeState::unplugged && battery.percent>=0 && battery.percent<=chargeReminder;
        if (low) box(384-measure(small,detail),22,measure(small,detail)+8,18,red);
        rightText(388,36,small,detail,low ? white : black);
    }
    box(12,44,376,1,black);
    text(13,64,small,district.station);
    if (problem[0] || oldObs) rightText(388,62,small,locationLabel);
    char temp[12]="--";
    if (obs.stamp[0]) number(temp,sizeof(temp),obs.temperature);
    const Font& size=measure(large,temp)<=121 ? large :
                     (measure(compactDigits,temp)<=121 ? compactDigits : title);
    const int end=text(9,133,size,temp);
    const int base=&size==&large ? 88 : (&size==&compactDigits ? 108 : 122);
    circle(end+4,base-14,3,black); circle(end+4,base-14,1,yellow);
    text(end+10,base,body,"C");
    const uint8_t* icon=obs.stamp[0] ? symbol(obs.icon) : nullptr;
    if (icon) {
        for (int y=0; y<68; ++y)
            for (int x=0; x<80; ++x)
                if (icon[y*10+x/8] & (0x80>>(x%8))) dot(142+x,67+y,black);
    }
    text(235,75,small,"HONG KONG");
    const char* wx=obs.stamp[0] ? condition(obs.icon) : "Awaiting data";
    text(234,105,measure(medium,wx)<=157 ? medium : caption,wx);
    char humidity[28]="HQ humidity   --";
    if (obs.stamp[0] && obs.humidity>=0) snprintf(humidity,sizeof(humidity),"HQ humidity   %d%%",obs.humidity);
    text(235,130,caption,humidity);

    char rain[48]="District max rain   --";
    if (district.rainfallArea[0] && obs.stamp[0] && obs.rainMax>=0) {
        char mm[14], interval[22];
        number(mm,sizeof(mm),obs.rainMax);
        snprintf(rain,sizeof(rain),"District max rain   %s mm",mm);
        snprintf(interval,sizeof(interval),"%.5s-%.5s HKT",obs.rainStart,obs.rainEnd);
        rightText(388,168,small,interval);
    }
    text(12,168,caption,district.rainfallArea[0] ? rain : "District location unavailable");
    forecastPoster(forecast,today,oldForecast);
    box(0,278,width,22,problem[0] ? red : black);
    char stamp[36]="No observation";
    if (obs.stamp[0]) snprintf(stamp,sizeof(stamp),"%s%s HKT",problem[0] ? "" : "Observed  ",obs.stamp);
    const char* status=problem[0] ? problem : (oldObs ? "LAST OBSERVATION" : locationLabel);
    text(12,294,small,status,white);
    rightText(388,294,small,stamp,white);
}

void setupPage(const char* ssid, const char* password) {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,44,black);
    text(16,31,medium,"Hong Kong weather",white);
    text(16,83,medium,"Wi-Fi and district");
    text(16,115,body,"Join this setup network on your phone.");
    text(16,150,small,"NETWORK"); text(108,150,body,ssid);
    text(16,182,small,"PASSWORD"); text(108,182,body,password);
    box(16,203,368,36,yellow);
    text(26,229,body,"Open   http://192.168.4.1");
    text(16,265,small,"Pick Wi-Fi and district, then save on your phone.");
    text(16,287,small,"Upper-right button closes setup. Timeout: 5 min.");
}
void setupClosed() {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,44,black);
    text(16,31,medium,"Hong Kong weather",white);
    text(16,99,medium,"Setup closed");
    text(16,146,body,"Wi-Fi still needs to be set up.");
    box(16,181,368,48,yellow);
    text(26,212,body,"Press lower-right to reopen setup.");
}

}
