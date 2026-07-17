#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdarg>
#include "WString.h"

class Print
{
public:
    virtual size_t write(uint8_t) = 0;
    virtual size_t write(const uint8_t *buffer, size_t size)
    {
        size_t n = 0;
        while (size--) n += write(*buffer++);
        return n;
    }

    size_t print(const char *str) { return str ? write((const uint8_t *)str, strlen(str)) : 0; }
    size_t print(const String &s) { return print(s.c_str()); }
    size_t print(char c) { return write((uint8_t)c); }
    size_t print(int v) { char b[32]; int n = snprintf(b, sizeof(b), "%d", v); return write((const uint8_t *)b, n); }
    size_t print(unsigned int v) { char b[32]; int n = snprintf(b, sizeof(b), "%u", v); return write((const uint8_t *)b, n); }
    size_t print(long v) { char b[32]; int n = snprintf(b, sizeof(b), "%ld", v); return write((const uint8_t *)b, n); }
    size_t print(unsigned long v) { char b[32]; int n = snprintf(b, sizeof(b), "%lu", v); return write((const uint8_t *)b, n); }
    size_t print(double v) { char b[64]; int n = snprintf(b, sizeof(b), "%f", v); return write((const uint8_t *)b, n); }

    size_t println() { return print("\r\n"); }
    size_t println(const char *str) { size_t n = print(str); return n + println(); }
    size_t println(const String &s) { return println(s.c_str()); }
    size_t println(char c) { size_t n = print(c); return n + println(); }
    size_t println(int v) { size_t n = print(v); return n + println(); }
    size_t println(unsigned int v) { size_t n = print(v); return n + println(); }
    size_t println(long v) { size_t n = print(v); return n + println(); }
    size_t println(unsigned long v) { size_t n = print(v); return n + println(); }
    size_t println(double v) { size_t n = print(v); return n + println(); }

    size_t printf(const char *fmt, ...)
    {
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        if (n < 0) return 0;
        if (n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        return write((const uint8_t *)buf, n);
    }
};

class Stream : public Print
{
public:
    virtual int available() = 0;
    virtual int read() = 0;
    virtual int peek() = 0;
    virtual void flush() {}

    virtual size_t readBytes(char *buffer, size_t length)
    {
        size_t count = 0;
        while (count < length) {
            int c = read();
            if (c < 0) break;
            *buffer++ = (char)c;
            count++;
        }
        return count;
    }
    size_t readBytes(uint8_t *buffer, size_t length)
    {
        return readBytes((char *)buffer, length);
    }
    String readString()
    {
        String ret;
        int c = read();
        while (c >= 0) { ret += (char)c; c = read(); }
        return ret;
    }
    String readStringUntil(char terminator)
    {
        String ret;
        int c = read();
        while (c >= 0 && (char)c != terminator) { ret += (char)c; c = read(); }
        return ret;
    }
    void setTimeout(unsigned long) {}
};
