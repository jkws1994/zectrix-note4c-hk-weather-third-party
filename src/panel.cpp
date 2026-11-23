// Four-colour command sequence adapted from the NOTE4C reference firmware.
// See NOTE4C-LICENSE.txt and README.md.
#include <Arduino.h>
#include <SPI.h>
#include <driver/gpio.h>
#include "panel.h"
#include "screen.h"

namespace {
constexpr int rail=6, busy=8, reset=9, dc=10, cs=11, sck=12, mosi=13;
SPIClass epd(HSPI);

void sendByte(bool data, uint8_t byte) {
    digitalWrite(dc, data);
    digitalWrite(cs, LOW);
    epd.transfer(byte);
    digitalWrite(cs, HIGH);
}

bool ready() {
    const uint32_t start = millis();
    while (digitalRead(busy) == LOW) {
        if (millis()-start >= 120000) {
            Serial.println("SSD2683 BUSY timeout");
            return false;
        }
        delay(25);
    }
    return true;
}

bool sendFrame() {
    if (!ready()) return false;
    sendByte(false,0xE9); sendByte(true,0x01);
    sendByte(false,0x10);
    if (!ready()) return false;
    digitalWrite(dc,HIGH);
    for (int y=0; y<screen::height; ++y) {
        digitalWrite(cs,LOW);
        epd.writeBytes(screen::frame+y*screen::stride,screen::stride);
        digitalWrite(cs,HIGH);
    }
    sendByte(false,0x04); delay(10);
    if (!ready()) return false;
    sendByte(false,0x12); sendByte(true,0x00); delay(10);
    if (!ready()) return false;
    sendByte(false,0x02); sendByte(true,0x00); delay(10);
    if (!ready()) return false;
    delay(20);
    sendByte(false,0x07); sendByte(true,0xA5);
    return true;
}
}

void initBoard() {
    // Re-establish the latch before releasing the deep-sleep pad hold.
    pinMode(17,OUTPUT); digitalWrite(17,HIGH);
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(GPIO_NUM_17);
    digitalWrite(17,HIGH);
    gpio_hold_dis(GPIO_NUM_6);
    pinMode(rail,OUTPUT); digitalWrite(rail,LOW);
    pinMode(3,OUTPUT); digitalWrite(3,HIGH);  // active-low LED
    pinMode(busy,INPUT);
    pinMode(dc,OUTPUT); pinMode(reset,OUTPUT);
    pinMode(cs,OUTPUT); digitalWrite(cs,HIGH);
    epd.begin(sck,-1,mosi,-1);
}

bool refreshPanel() {
    digitalWrite(rail,HIGH); delay(10);
    digitalWrite(reset,HIGH); delay(10);
    digitalWrite(reset,LOW); delay(20);
    digitalWrite(reset,HIGH); delay(10);
    epd.beginTransaction(SPISettings(8000000,MSBFIRST,SPI_MODE0));
    const bool ok = sendFrame();
    epd.endTransaction();
    digitalWrite(rail,LOW);
    return ok;
}
