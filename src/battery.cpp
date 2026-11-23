#include "battery.h"
#include <Arduino.h>
#include <algorithm>

Battery readBattery() {
    constexpr int sensePin=4, chargePin=2, fullPin=1;
    pinMode(chargePin,INPUT);
    pinMode(fullPin,INPUT);
    analogSetPinAttenuation(sensePin,ADC_11db);

    // Check for a steady charger state across the reference board's 400 ms window.
    int charging=0, full=0, sum=0;
    bool validVoltage=true;
    for (int i=0; i<21; ++i) {
        charging += digitalRead(chargePin)==LOW;
        full += digitalRead(fullPin)==HIGH;
        if (i%2==1) {
            // The divider supplies half the cell voltage to ADC1.
            const int mv=2*int(analogReadMilliVolts(sensePin));
            validVoltage &= mv>=2500 && mv<=4450;
            sum+=mv;
        }
        if (i<20) delay(20);
    }
    Battery battery;
    if (charging==0 && full==0) battery.state=ChargeState::unplugged;
    else if (charging==21 && full==0) battery.state=ChargeState::charging;
    else if (full==21 && charging==0) battery.state=ChargeState::full;
    if (validVoltage && battery.state!=ChargeState::unknown) {
        // Polynomial from the ZECTRIX NOTE4C reference battery reader, in millivolts.
        const int mv=sum/10;
        const int estimate=(mv*(9016-mv)-19189000)/10000;
        battery.percent=std::max(0,std::min(100,estimate));
        if (battery.state==ChargeState::full) battery.percent=100;
    }
    return battery;
}

time_t estimateChargeTime(DischargeHistory& history, const Battery& battery, time_t now) {
    if (!now || battery.state!=ChargeState::unplugged || battery.percent<=chargeReminder) {
        history={};
        return 0;
    }
    if (!history.start || now<history.last || now-history.last>2*3600 ||
        battery.percent>history.lastPercent+2 || battery.percent>history.initialPercent) {
        history={now,now,battery.percent,battery.percent};
        return 0;
    }
    history.last=now;
    history.lastPercent=battery.percent;
    const int drop=history.initialPercent-battery.percent;
    const time_t elapsed=now-history.start;
    if (drop<5 || elapsed<6*3600) return 0;
    const int64_t secondsLeft=int64_t(elapsed)*(battery.percent-chargeReminder)/drop;
    if (secondsLeft>30*24*3600) return 0;
    // Round up to an hour and avoid publishing dates outside the platform time range.
    const int64_t hour=(int64_t(now)+secondsLeft+3599)/3600*3600;
    return hour>2147483647-8*3600 ? 0 : time_t(hour);
}
