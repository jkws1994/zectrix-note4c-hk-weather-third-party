#pragma once
#include <Arduino.h>

// HTTPClient removes chunk framing. Bound the decoded response before JSON parsing.
class HttpBody : public Stream {
    const size_t limit;
public:
    explicit HttpBody(size_t limit): limit(limit) {}
    String bytes;
    size_t write(uint8_t b) override { return write(&b,1); }
    size_t write(const uint8_t* buf,size_t n) override {
        if (n>limit-bytes.length()) return 0;
        return bytes.concat(reinterpret_cast<const char*>(buf),n) ? n : 0;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};
