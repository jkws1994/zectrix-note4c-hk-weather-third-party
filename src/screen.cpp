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
#include "hk_glyphs.inc"

static const Font warningCompact=small;
static Words tr{Language::english};
void setLanguage(Language language) { tr.language=language; }

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

// Each embedded HK glyph has one full-width cell. ASCII keeps the original
// proportional metrics. Drawing and measuring share this UTF-8 iterator.
static uint32_t nextCode(const char*& s) {
    const auto first=static_cast<uint8_t>(*s++);
    if (first<128) return first;
    unsigned extra=first<0xe0 ? 1 : first<0xf0 ? 2 : 3;
    if (first<0xc2 || first>0xf4) return '?';
    uint32_t code=first & (0x7f >> (extra+1));
    for (unsigned i=0; i<extra; ++i) {
        const auto byte=static_cast<uint8_t>(*s);
        if ((byte & 0xc0)!=0x80) return '?';
        code=(code<<6) | (byte&63);
        ++s;
    }
    return code;
}

struct Cell { const uint8_t* bits; int left, top, w, h, advance; };
static Cell cell(const Font& f, uint32_t code) {
    if (code>=128 && code<=0xffff) {
        const auto end=hkCodes+sizeof(hkCodes)/sizeof(hkCodes[0]);
        const auto found=std::lower_bound(hkCodes,end,code);
        if (found!=end && *found==code) {
            const HKFont& face=(&f==&warningCompact) ? hk12 : (&f==&title) ? hk32 : (&f==&medium) ? hk24 :
                               (&f==&body || &f==&caption) ? hk18 : hk14;
            const int size=face.size;
            return {face.bits+(found-hkCodes)*size*((size+7)/8),
                    0,-face.ascent,size,size,size};
        }
    }
    const Glyph* g=glyph(f,code<128 ? char(code) : '?');
    if (!g) return {nullptr,0,0,0,0,0};
    return {f.bits+g->offset,g->left,g->top,g->w,g->h,g->advance};
}

static int measure(const Font& f, const char* s) {
    int width=0;
    while (*s) width+=cell(f,nextCode(s)).advance;
    return width;
}

static int text(int x, int baseline, const Font& f, const char* s, Ink ink = black) {
    while (*s) {
        const Cell g=cell(f,nextCode(s));
        for (int y=0; y<g.h; ++y)
            for (int i=0; i<g.w; ++i)
                if (g.bits[y*((g.w+7)/8)+i/8] & (0x80 >> (i%8)))
                    dot(x+g.left+i,baseline+g.top+y,ink);
        x+=g.advance;
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

// Render the original 80 x 68 symbol at the size available on each page.
static void weatherSymbol(int left,int top,int divisor,int code) {
    const uint8_t* icon=symbol(code);
    if (!icon) return;
    for (int y=0; y<68; ++y)
        for (int x=0; x<80; ++x)
            if (icon[y*10+x/8] & (0x80>>(x%8))) dot(left+x*3/divisor,top+y*3/divisor,black);
}

static void rightText(int right, int baseline, const Font& font, const char* label, Ink ink=black) {
    text(right-measure(font,label),baseline,font,label,ink);
}

static void observationFooter(const char* label,const char* stamp,bool problem) {
    box(0,278,width,22,problem ? red : black);
    text(8,294,small,label,white);
    char when[48];
    snprintf(when,sizeof(when),"%s",tr("No observation","暫無觀測紀錄"));
    if (stamp[0]) snprintf(when,sizeof(when),tr("Obs. %s HKT","觀測 %s 香港時間"),stamp);
    rightText(392,294,small,when,white);
}

static const char* monthName(const char* digits) {
    static const char* const months[]={"JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    int m=(digits[0]-'0')*10+digits[1]-'0';
    return (m>=1 && m<=12) ? months[m-1] : "";
}

static void forecastPoster(const Forecast& forecast, const char* today, bool saved, bool warningsAvailable) {
    constexpr int left=176, right=390;
    box(left,122,right-left,2,black);
    text(left,142,small,saved && forecast.count ? tr("Saved HK forecast","上次預報") : tr("HK 3-day forecast","全港三天預報"));
    rightText(right,142,small,tr("Low / high","最低 / 最高"));
    if (!warningsAvailable) text(left,159,small,tr("HKO warnings unavailable","未能取得天文台警告"),red);

    int row=0;
    for (int i=0; i<forecast.count && i<3; ++i) {
        const Day& day=forecast.days[i];
        if (today[0] && strcmp(day.date,today)<0) continue;
        const int y=(warningsAvailable ? 150 : 162)+row*(warningsAvailable ? 42 : 38);
        if (row++) box(left,y-2,right-left,1,black);
        char date[32], low[12], high[12], range[32];
        if (tr.chinese()) {
            static const char* const days[][2]={{"Mon","一"},{"Tue","二"},{"Wed","三"},{"Thu","四"},
                                                {"Fri","五"},{"Sat","六"},{"Sun","日"}};
            const char* weekday="";
            for (const auto& entry : days) if (strcmp(day.weekday,entry[0])==0) weekday=entry[1];
            const int month=(day.date[4]-'0')*10+day.date[5]-'0';
            const int monthDay=(day.date[6]-'0')*10+day.date[7]-'0';
            snprintf(date,sizeof(date),"%d月%d日 %s%s",month,monthDay,weekday[0] ? "週" : "",weekday);
        } else snprintf(date,sizeof(date),"%.3s %.2s %s",day.weekday,day.date+6,monthName(day.date+4));
        number(low,sizeof(low),day.low);
        number(high,sizeof(high),day.high);
        snprintf(range,sizeof(range),"%s / %s",low,high);
        const int rangeRoom=right-7-left-measure(small,date)-4;
        const Font& digits=measure(medium,range)<=std::min(118,rangeRoom) ? medium : caption;
        text(left,y+17,small,date);
        rightText(right-7,y+20,digits,range);
        const int degreeY=y+(&digits==&medium ? 2 : 9);
        circle(right-3,degreeY,2,black); dot(right-3,degreeY,white);
        weatherSymbol(left,y+18,12,day.icon);
        text(left+25,y+34,small,condition(day.icon,tr.language));
    }
    if (!row) text(left,208,body,tr("Forecast unavailable","暫無天氣預報"));
}

#include "warning_icons.inc"

static Ink signalInk(SignalColour colour) {
    switch (colour) {
        case SignalColour::amber: return yellow;
        case SignalColour::black: return black;
        default: return red;
    }
}

static void warningBand(int x,int y,int w,int h,const WarningSignal& lead,bool available) {
    // Cached warnings keep the signal colours on the icons. An outlined heading
    // distinguishes them from the solid heading used after a successful check.
    box(x,y,w,h,available ? signalInk(lead.colour) : black);
    if (!available) box(x+1,y+1,w-2,h-2,white);
}

static Ink warningHeadingInk(const WarningSignal& lead,bool available) {
    return !available || lead.colour==SignalColour::amber ? black : white;
}

static void drawWarningIcon(int x,int y,const WarningSignal& signal,int scale) {
    const Ink ground=signal.colour==SignalColour::neutral ? white : signalInk(signal.colour);
    const Ink stroke=ground==white || ground==yellow ? black : white;
    const int size=16*scale;
    box(x,y,size,size,black);
    box(x+1,y+1,size-2,size-2,ground);
    const auto& mask=warningMasks[static_cast<unsigned>(signal.icon)];
    for (int row=0; row<16; ++row)
        for (int col=0; col<16; ++col)
            if (mask[row] & (0x8000u >> col)) box(x+scale*col,y+scale*row,scale,scale,stroke);
}

static void warningRow(int x,int y,const WarningSignal& signal,int textWidth) {
    drawWarningIcon(x,y,signal,1);
    char label[64];
    snprintf(label,sizeof(label),"%s",tr(signal.label,signal.labelHK));
    if (measure(small,label)>textWidth) {
        // Both panels already identify these as warnings. Keep the level and direction.
        char* suffix=strstr(label," Warning");
        if (suffix && suffix[8]=='\0') *suffix='\0';
    }
    const Font& font=tr.chinese() && measure(small,label)>textWidth ? warningCompact : small;
    text(x+20,y+13,font,label);
}

static void warningPoster(uint32_t remaining, bool available) {
    const auto* lead=takeWarningSignal(remaining);
    if (!lead) return;
    const Ink foreground=warningHeadingInk(*lead,available);
    warningBand(0,44,width,24,*lead,available);
    text(12,62,caption,available ? tr("HKO warnings in force","天文台生效警告") : tr("Last known HKO warnings","上次收到的天文台警告"),foreground);
    constexpr int extraRows=4;
    const int extra=__builtin_popcount(remaining);
    if (extra>extraRows) {
        char more[16];
        snprintf(more,sizeof(more),tr("+%d more","另有%d項"),extra-extraRows);
        rightText(388,61,small,more,foreground);
    }

    const int leadY=extra ? 72 : 84;
    drawWarningIcon(12,leadY,*lead,2);
    const char* label=tr(lead->label,lead->labelHK);
    const Font& font=measure(medium,label)<=336 ? medium : body;
    text(52,leadY+25,font,label);
    for (int i=0; i<extraRows; ++i) {
        const auto* signal=takeWarningSignal(remaining);
        if (!signal) break;
        warningRow(12+(i%2)*190,107+(i/2)*18,*signal,166);
    }
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
    if (battery.state==ChargeState::charging) strcpy(detail,tr("Charging","充電中"));
    else if (battery.state==ChargeState::full && battery.percent>=0) strcpy(detail,tr("Fully charged","已充滿電"));
    else if (battery.percent>=0 && battery.percent<=chargeReminder) strcpy(detail,tr("Charge now","請充電"));
    else if (battery.chargeAt) {
        time_t local=battery.chargeAt+8*3600;
        struct tm hkt{};
        gmtime_r(&local,&hkt);
        if (tr.chinese())
            snprintf(detail,sizeof(detail),"約%d月%d日 %02d:%02d 充電",
                     hkt.tm_mon+1,hkt.tm_mday,hkt.tm_hour,hkt.tm_min);
        else strftime(detail,sizeof(detail),"Charge ~%d %b %H:%M",&hkt);
    }
}

static int drawBatteryStatus(const Battery& battery) {
    char level[20]{}, detail[32]{};
    batteryHeader(battery,level,detail);
    const int levelWidth=batteryIconWidth+batteryIconGap+measure(caption,level);
    const int baseline=detail[0] ? 19 : 28;
    drawBatteryIcon(388-levelWidth,baseline-13,battery.percent);
    rightText(388,baseline,caption,level);
    if (detail[0]) {
        const bool low=battery.state==ChargeState::unplugged && battery.percent>=0 && battery.percent<=chargeReminder;
        if (low) box(384-measure(small,detail),22,measure(small,detail)+8,18,red);
        rightText(388,36,small,detail,low ? white : black);
    }
    return std::max(levelWidth,measure(small,detail));
}

void weather(const District& district, const Observation& obs, const Forecast& forecast, const char* today,
             bool oldObs, bool oldForecast, const char* problem, const char* locationLabel, const Battery& battery,
             WarningDisplay warnings) {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,44,yellow);
    const int badgeWidth=drawBatteryStatus(battery);
    const int split=width-24-badgeWidth;
    box(0,0,split,44,black);
    const char* name=tr(district.name,district.nameHK);
    const Font& heading=measure(title,name)<=split-20 ? title :
                        (measure(medium,name)<=split-20 ? medium : body);
    text(10,33,heading,name,white);

    if (warnings.active) warningPoster(warnings.active,warnings.available);
    const int top=warnings.active ? 144 : 44;
    box(0,top,164,278-top,yellow);
    // Keep real station names intact, including names that need two lines.
    char station[64];
    snprintf(station,sizeof(station),"%s",tr(district.station,district.stationHK));
    const Font& stationFont=measure(caption,station)<=144 ? caption : small;
    char* lineBreak=nullptr;
    if (measure(stationFont,station)>144) {
        lineBreak=strchr(station+strlen(station)/2,' ');
        if (!lineBreak) lineBreak=strrchr(station,' ');
    }
    if (lineBreak) {
        *lineBreak='\0';
        text(10,top+39,stationFont,lineBreak+1);
    }
    text(10,top+22,stationFont,station);

    char temp[12]="--";
    if (obs.stamp[0]) number(temp,sizeof(temp),obs.temperature);
    const Font& digits=!lineBreak && measure(large,temp)<=121 ? large :
                       (measure(compactDigits,temp)<=121 ? compactDigits : title);
    const int tempBase=top+89;
    const int end=text(7,tempBase,digits,temp);
    const int unitY=tempBase-(&digits==&large ? 58 : (&digits==&compactDigits ? 38 : 20));
    circle(end+5,unitY,3,black); circle(end+5,unitY,1,yellow);
    text(end+11,unitY+13,body,"C");

    constexpr int left=176,right=390;
    text(left,top+20,small,tr("District max rain","區內最高雨量"));
    rightText(right,top+20,small,tr("HQ humidity","天文台濕度"));
    char rain[24]="--", humidity[12]="--";
    if (district.rainfallArea[0] && obs.stamp[0] && obs.rainMax>=0) {
        char mm[14], interval[24];
        number(mm,sizeof(mm),obs.rainMax);
        snprintf(rain,sizeof(rain),tr("%s mm","%s 毫米"),mm);
        snprintf(interval,sizeof(interval),tr("%.5s-%.5s HKT","%.5s-%.5s"),obs.rainStart,obs.rainEnd);
        text(left,top+68,small,interval);
    } else if (!district.rainfallArea[0]) {
        text(left,top+68,small,tr("District unavailable","暫無分區雨量"));
    }
    if (obs.stamp[0] && obs.humidity>=0) snprintf(humidity,sizeof(humidity),"%d%%",obs.humidity);
    text(left,top+49,measure(medium,rain)<=132 ? medium : small,rain);
    rightText(right,top+49,medium,humidity);

    const char* wx=obs.stamp[0] ? condition(obs.icon,tr.language) : tr("Awaiting data","等候資料");
    if (warnings.active) {
        box(left,top+77,right-left,1,black);
        if (obs.stamp[0]) weatherSymbol(left,top+84,6,obs.icon);
        text(left+49,top+97,small,tr("Hong Kong","香港"));
        text(left+49,top+124,measure(medium,wx)<=165 ? medium : caption,wx);
    } else {
        box(10,159,144,1,black);
        text(10,180,small,tr("Hong Kong conditions","全港天氣"));
        if (obs.stamp[0]) weatherSymbol(10,187,4,obs.icon);
        text(10,265,measure(medium,wx)<=144 ? medium : caption,wx);
        forecastPoster(forecast,today,oldForecast,warnings.available);
    }
    if (problem[0] || oldObs)
        text(10,warnings.active ? 272 : 151,small,locationLabel);
    const char* status=problem[0] ? problem : (oldObs ? tr("LAST OBSERVATION","上次觀測") : locationLabel);
    observationFooter(status,obs.stamp,problem[0]);
}

struct MapPixel { int x, y; };

static MapPixel project(DistrictPoint point) {
    // Bounds of the official geometry, including administrative waters.
    constexpr int32_t west=113817105, east=114502469, south=22136728, north=22568330;
    return {6+((point.x-west)*178+(east-west)/2)/(east-west),
            91+((north-point.y)*122+(north-south)/2)/(north-south)};
}

static void line(MapPixel from, MapPixel to, Ink ink) {
    const int dx=std::abs(to.x-from.x), sx=from.x<to.x ? 1 : -1;
    const int dy=-std::abs(to.y-from.y), sy=from.y<to.y ? 1 : -1;
    int error=dx+dy;
    while (true) {
        dot(from.x,from.y,ink);
        if (from.x==to.x && from.y==to.y) return;
        const int step=2*error;
        if (step>=dy) { error+=dy; from.x+=sx; }
        if (step<=dx) { error+=dx; from.y+=sy; }
    }
}

static void districtOutline(int index, Ink ink) {
    const auto boundary=districtBoundary(index);
    auto previous=project(boundary.points[boundary.count-1]);
    for (unsigned i=0; i<boundary.count; ++i) {
        const auto next=project(boundary.points[i]);
        line(previous,next,ink);
        previous=next;
    }
}

static void fillSelectedDistrict(int index) {
    const auto boundary=districtBoundary(index);
    // Mark odd scanline crossings in the blank map area, then colour its interior.
    // Skip horizontal edges and their upper endpoint so shared vertices count once.
    auto a=project(boundary.points[boundary.count-1]);
    int top=a.y, bottom=a.y;
    for (unsigned i=0; i<boundary.count; ++i) {
        const auto b=project(boundary.points[i]);
        top=std::min(top,b.y);
        bottom=std::max(bottom,b.y);
        const auto low=a.y<b.y ? a : b;
        const auto high=a.y<b.y ? b : a;
        for (int y=low.y; y<high.y; ++y) {
            const int x=low.x+(y-low.y)*(high.x-low.x)/(high.y-low.y);
            frame[y*stride+x/4] ^= 1u << (6-2*(x%4)); // White <-> black.
        }
        a=b;
    }
    for (int y=top; y<bottom; ++y) {
        bool inside=false;
        for (int x=6; x<=184; ++x) {
            if (((frame[y*stride+x/4]>>(6-2*(x%4)))&3)==black) inside=!inside;
            dot(x,y,inside ? yellow : white);
        }
    }
}

static void mapOutlines(int selected) {
    struct Marker { MapPixel anchor, label; };
    // Short leaders move the dense urban labels clear of their boundaries.
    static constexpr Marker markers[districtCount] = {
        {{96,165},{76,172}}, {{91,172},{81,187}}, {{112,174},{130,183}},
        {{43,185},{41,194}}, {{103,162},{102,159}}, {{81,157},{73,150}},
        {{112,163},{130,165}}, {{127,101},{125,98}}, {{152,168},{171,172}},
        {{105,141},{108,137}}, {{94,157},{90,144}}, {{109,187},{108,202}},
        {{155,107},{152,113}}, {{65,155},{55,161}}, {{43,145},{38,145}},
        {{100,174},{99,185}}, {{106,154},{120,147}}, {{63,123},{62,122}},
    };
    if (selected>=0 && selected<districtCount) fillSelectedDistrict(selected);
    for (int i=0; i<districtCount; ++i) districtOutline(i,black);
    if (selected>=0 && selected<districtCount) districtOutline(selected,red);
    for (int i=0; i<districtCount; ++i) {
        const auto& marker=markers[i];
        const Ink ink=i==selected ? red : black;
        line(marker.anchor,marker.label,ink);
    }
    for (int i=0; i<districtCount; ++i) {
        const auto mark=markers[i].label;
        const char letter[2]={char('A'+i),0};
        const int start=mark.x-measure(small,letter)/2;
        box(start-2,mark.y-6,measure(small,letter)+4,13,i==selected ? red : white);
        text(start,mark.y+5,small,letter,i==selected ? white : black);
    }
}

void districtMap(const DistrictWeather& weather,int selected,const char* status) {
    memset(frame,0x55,sizeof(frame));
    constexpr int divider=189;
    box(0,0,divider,28,yellow);
    text(9,21,body,tr("District map","十八區地圖"));
    text(195,21,small,tr("Station temperatures","各站氣溫"));
    circle(380,11,2,black);
    dot(380,11,white);
    rightText(395,21,small,"C");
    box(0,27,width,1,black);
    box(divider,0,1,height,black);
    mapOutlines(selected);

    if (selected>=0 && selected<districtCount) {
        const char key[]={char('A'+selected),0};
        box(8,226,17,17,red);
        text(16-measure(small,key)/2,239,small,key,white);
        text(30,239,small,tr(districts[selected].name,districts[selected].nameHK));
    }
    if (status[0]) {
        text(8,274,small,status,red);
        if (weather.stamp[0]) {
            char when[40];
            snprintf(when,sizeof(when),tr("%s HKT","%s 香港時間"),weather.stamp);
            text(8,292,small,when);
        }
    }
    for (int i=0; i<districtCount; ++i) {
        const int baseline=40+15*i;
        const char key[]={char('A'+i),0};
        if (i==selected) {
            box(divider+1,baseline-11,width-divider-1,15,yellow);
            box(193,baseline-11,13,15,red);
        }
        text(199-measure(small,key)/2,baseline,small,key,i==selected ? white : black);
        text(209,baseline,listFont,tr(districts[i].station,districts[i].stationHK));
        char value[8]="--";
        if (weather.present & (uint32_t(1)<<i))
            snprintf(value,sizeof(value),"%.0f",weather.temperatures[i]);
        rightText(395,baseline,small,value);
    }
}

void setupPage(const char* ssid, const char* password) {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,44,black);
    text(16,31,medium,tr("Hong Kong weather","香港天氣"),white);
    if (!tr.chinese()) text(16,60,small,"手機設定可選繁體中文（香港）");
    text(16,83,medium,tr("Wi-Fi and district","網絡及地區設定"));
    text(16,115,body,tr("Join this setup network on your phone.","用手機連接以下 Wi-Fi 網絡。"));
    text(16,150,small,tr("NETWORK","網絡名稱")); text(108,150,body,ssid);
    text(16,182,small,tr("PASSWORD","密碼")); text(108,182,body,password);
    box(16,203,368,36,yellow);
    text(26,229,body,tr("Open   http://192.168.4.1","開啟 http://192.168.4.1"));
    text(16,265,small,tr("Pick Wi-Fi and district, then save on your phone.","喺手機揀好 Wi-Fi 同地區，再儲存設定。"));
    text(16,287,small,tr("Upper-right button closes setup. Timeout: 5 min.","撳右上鍵離開，或等 5 分鐘自動關閉。"));
}
void setupClosed() {
    memset(frame,0x55,sizeof(frame));
    box(0,0,width,44,black);
    text(16,31,medium,tr("Hong Kong weather","香港天氣"),white);
    text(16,99,medium,tr("Setup closed","設定已關閉"));
    text(16,146,body,tr("Wi-Fi still needs to be set up.","請先設定 Wi-Fi 網絡。"));
    box(16,181,368,48,yellow);
    text(26,212,body,tr("Hold lower-right to reopen setup.","長撳右下鍵，再開設定。"));
}

}
