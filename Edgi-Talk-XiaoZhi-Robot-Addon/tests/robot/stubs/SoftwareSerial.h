#pragma once
#include "Servo.h"
#include <deque>
#include <string>
class SoftwareSerial {
public:
    std::deque<char> input;
    std::string output;
    unsigned long baud = 0;
    bool overflow_flag = false;
    SoftwareSerial(int, int) {}
    void begin(unsigned long value) { baud = value; }
    int available() { return static_cast<int>(input.size()); }
    bool overflow() { bool flag = overflow_flag; overflow_flag = false; return flag; }
    int read() { char c = input.front(); input.pop_front(); return (unsigned char)c; }
    void print(const __FlashStringHelper *s) { output += reinterpret_cast<const char *>(s); }
    void print(int n) { output += std::to_string(n); }
    void println(const __FlashStringHelper *s) { print(s); output += "\r\n"; }
    void println(int n) { print(n); output += "\r\n"; }
};
