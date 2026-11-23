#include "board_clock.h"
#include "refresh.h"
#include <Wire.h>
#include <sys/time.h>
#include <cstring>

namespace boardClock {
namespace {
constexpr uint8_t address=0x51;
// Leave room for the display's conversion to HKT with the target's signed 32-bit time_t.
constexpr int64_t latestUtc=2147483647-8*3600;
bool supported(int64_t utc) { return utc>=refresh::validClock && utc<=latestUtc; }

int decimal(uint8_t bcd) {
    if ((bcd & 15)>9 || (bcd>>4)>9) return -1;
    return 10*(bcd>>4)+(bcd & 15);
}
uint8_t bcd(int value) { return (value/10)*16+value%10; }
}

time_t phoneTime(const char* text) {
    if (strlen(text)!=10) return 0;
    int64_t utc=0;
    for (int i=0; i<10; ++i) {
        if (text[i]<'0' || text[i]>'9') return 0;
        utc=10*utc+text[i]-'0';
    }
    return supported(utc) ? time_t(utc) : 0;
}

bool setSystem(time_t utc) {
    if (!supported(utc)) return false;
    timeval value{utc,0};
    return settimeofday(&value,nullptr)==0;
}

time_t readRtc() {
    if (!Wire.begin(47,48,100000)) return 0;
    Wire.beginTransmission(address);
    Wire.write(uint8_t(0));
    if (Wire.endTransmission(false)!=0 || Wire.requestFrom(address,uint8_t(9))!=9) return 0;
    uint8_t r[9];
    for (auto& byte : r) byte=Wire.read();
    if ((r[0]&0x20) || (r[2]&0x80)) return 0; // STOP or voltage-low integrity flag.
    const int year=decimal(r[8]), month=decimal(r[7]&0x1f), day=decimal(r[5]&0x3f);
    const int hour=decimal(r[4]&0x3f), minute=decimal(r[3]&0x7f), second=decimal(r[2]&0x7f);
    if (year<24 || year>38 || month<1 || month>12 || day<1 || day>31 ||
        hour<0 || hour>23 || minute<0 || minute>59 || second<0 || second>59) return 0;
    // RTC fields written by this firmware are UTC. Count from 2000-01-01.
    static const int daysBeforeMonth[]={0,31,59,90,120,151,181,212,243,273,304,334};
    const int days=365*year+(year+3)/4+daysBeforeMonth[month-1]+
                   (month>2 && year%4==0)+day-1;
    const int64_t utc=946684800LL+int64_t(days)*86400+hour*3600+minute*60+second;
    if (!supported(utc)) return 0;
    const time_t result=utc;
    struct tm check{};
    if (!gmtime_r(&result,&check) || check.tm_mday!=day) return 0;
    return result;
}

bool writeRtc(time_t utc) {
    struct tm date{};
    if (!supported(utc) || !gmtime_r(&utc,&date) || !Wire.begin(47,48,100000)) return false;
    Wire.beginTransmission(address);
    Wire.write(uint8_t(0)); // Control_status_1: normal oscillator and test modes.
    Wire.write(uint8_t(0));
    if (Wire.endTransmission()!=0) return false;
    const uint8_t dateBytes[]={bcd(date.tm_sec),bcd(date.tm_min),bcd(date.tm_hour),bcd(date.tm_mday),
                               bcd(date.tm_wday),bcd(date.tm_mon+1),bcd(date.tm_year-100)};
    Wire.beginTransmission(address);
    Wire.write(uint8_t(2));
    Wire.write(dateBytes,sizeof(dateBytes)); // Includes clearing VL after a known time is set.
    return Wire.endTransmission()==0;
}
}
