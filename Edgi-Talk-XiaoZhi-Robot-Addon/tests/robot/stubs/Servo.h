#pragma once
#include <stdint.h>
#include <stddef.h>
class __FlashStringHelper;
#define F(s) reinterpret_cast<const __FlashStringHelper *>(s)
unsigned long millis();
class Servo {
public:
    int angle = 90;
    void attach(int) {}
    void write(int value) { angle = value; }
};
