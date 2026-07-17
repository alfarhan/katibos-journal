#pragma once
// Arduino-compatible String for host builds.
//
// CRITICAL design constraint: firmware code passes String straight into
// printf("%s", str). clang inserts a runtime trap (and the program aborts)
// when a *non-trivially-copyable* type is passed through C varargs -- and
// -Wno-non-pod-varargs only silences the diagnostic, not the trap. So String
// must be trivially copyable: a plain inline char buffer, no heap, no user
// destructor, no user copy ctor. The buffer is the first member so "%s" reads
// it as a char*, exactly like Arduino's String works on-device.
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>

#ifndef HOST_STRING_CAP
#define HOST_STRING_CAP 9000
#endif

class String
{
public:
    char buf[HOST_STRING_CAP]; // MUST be first member: printf("%s", str) reads it

private:
    void set(const char *p, size_t n)
    {
        if (n >= HOST_STRING_CAP) n = HOST_STRING_CAP - 1;
        if (p && n) memcpy(buf, p, n);
        buf[n] = 0;
    }

public:
    String() { buf[0] = 0; }
    String(const char *p) { set(p, p ? strlen(p) : 0); }
    String(const std::string &o) { set(o.data(), o.size()); }
    String(char c) { buf[0] = c; buf[1] = 0; }
    String(int v) { snprintf(buf, sizeof(buf), "%d", v); }
    String(unsigned int v) { snprintf(buf, sizeof(buf), "%u", v); }
    String(long v) { snprintf(buf, sizeof(buf), "%ld", v); }
    String(unsigned long v) { snprintf(buf, sizeof(buf), "%lu", v); }
    String(double v, int dec = 2) { snprintf(buf, sizeof(buf), "%.*f", dec, v); }
    String(float v, int dec = 2) { snprintf(buf, sizeof(buf), "%.*f", dec, (double)v); }
    // Trivial copy/assign/destructor are intentionally compiler-generated.

    const char *c_str() const { return buf; }
    unsigned int length() const { return (unsigned int)strlen(buf); }
    bool isEmpty() const { return buf[0] == 0; }

    String &operator=(const char *p) { set(p, p ? strlen(p) : 0); return *this; }

    String &operator+=(const char *p)
    {
        if (p && *p) {
            size_t a = strlen(buf), b = strlen(p);
            if (a + b >= HOST_STRING_CAP) b = HOST_STRING_CAP - 1 - a;
            if (b > 0) { memcpy(buf + a, p, b); buf[a + b] = 0; }
        }
        return *this;
    }
    String &operator+=(const String &o) { return *this += o.buf; }
    String &operator+=(char c) { char t[2] = {c, 0}; return *this += t; }
    String &operator+=(int v) { return *this += String(v).buf; }

    // ArduinoJson's ArduinoStringWriter uses concat()
    unsigned int concat(const char *p) { *this += p; return length(); }
    unsigned int concat(const char *p, unsigned int n) { char t[HOST_STRING_CAP]; if (n >= HOST_STRING_CAP) n = HOST_STRING_CAP - 1; memcpy(t, p, n); t[n] = 0; *this += t; return length(); }
    unsigned int concat(const String &o) { *this += o; return length(); }
    unsigned int concat(char c) { *this += c; return length(); }

    friend String operator+(const String &a, const String &b) { String r(a); r += b; return r; }
    friend String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
    friend String operator+(const char *a, const String &b) { String r(a); r += b; return r; }
    friend String operator+(const String &a, char b) { String r(a); r += b; return r; }

    bool operator==(const String &o) const { return strcmp(buf, o.buf) == 0; }
    bool operator==(const char *p) const { return strcmp(buf, p ? p : "") == 0; }
    bool operator!=(const String &o) const { return !(*this == o); }
    bool operator!=(const char *p) const { return !(*this == p); }
    bool operator<(const String &o) const { return strcmp(buf, o.buf) < 0; }
    friend bool operator==(const char *p, const String &o) { return o == p; }

    bool equals(const String &o) const { return *this == o; }
    bool equals(const char *p) const { return *this == p; }

    char operator[](int i) const { return (i >= 0 && i < (int)strlen(buf)) ? buf[i] : 0; }
    char &operator[](int i) { return buf[i]; }
    char charAt(int i) const { return (*this)[i]; }

    String substring(unsigned int a) const
    {
        unsigned int n = length();
        if (a >= n) return String();
        return String(buf + a);
    }
    String substring(unsigned int a, unsigned int b) const
    {
        unsigned int n = length();
        if (a > n) a = n;
        if (b > n) b = n;
        if (b < a) b = a;
        String r;
        r.set(buf + a, b - a);
        return r;
    }

    bool startsWith(const char *p) const { return strncmp(buf, p ? p : "", strlen(p ? p : "")) == 0; }
    bool startsWith(const String &p) const { return startsWith(p.buf); }
    bool endsWith(const char *p) const
    {
        size_t a = strlen(buf), b = strlen(p ? p : "");
        if (b > a) return false;
        return strcmp(buf + a - b, p ? p : "") == 0;
    }
    bool endsWith(const String &p) const { return endsWith(p.buf); }

    int indexOf(char c) const { const char *p = strchr(buf, c); return p ? (int)(p - buf) : -1; }
    int indexOf(char c, int from) const { if (from < 0 || from > (int)length()) return -1; const char *p = strchr(buf + from, c); return p ? (int)(p - buf) : -1; }
    int indexOf(const char *t) const { const char *p = strstr(buf, t ? t : ""); return p ? (int)(p - buf) : -1; }
    int indexOf(const String &t) const { return indexOf(t.buf); }
    int indexOf(const String &t, int from) const { if (from < 0 || from > (int)length()) return -1; const char *p = strstr(buf + from, t.buf); return p ? (int)(p - buf) : -1; }
    int lastIndexOf(char c) const { const char *p = strrchr(buf, c); return p ? (int)(p - buf) : -1; }

    void remove(unsigned int idx) { if (idx < length()) buf[idx] = 0; }
    void remove(unsigned int idx, unsigned int count)
    {
        unsigned int n = length();
        if (idx >= n) return;
        if (idx + count > n) count = n - idx;
        memmove(buf + idx, buf + idx + count, n - idx - count + 1);
    }

    void trim()
    {
        char *start = buf;
        while (*start && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) start++;
        char *end = buf + strlen(buf);
        while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) end--;
        size_t n = end - start;
        memmove(buf, start, n);
        buf[n] = 0;
    }

    void replace(const char *from, const char *to)
    {
        if (!from || !*from) return;
        String result;
        const char *p = buf;
        size_t flen = strlen(from);
        const char *hit;
        while ((hit = strstr(p, from)) != nullptr) {
            char seg[HOST_STRING_CAP];
            size_t sl = hit - p;
            if (sl >= HOST_STRING_CAP) sl = HOST_STRING_CAP - 1;
            memcpy(seg, p, sl); seg[sl] = 0;
            result += seg;
            result += (to ? to : "");
            p = hit + flen;
        }
        result += p;
        *this = result;
    }
    void replace(const String &from, const String &to) { replace(from.buf, to.buf); }
    void replace(char from, char to) { for (char *c = buf; *c; c++) if (*c == from) *c = to; }

    void toUpperCase() { for (char *c = buf; *c; c++) *c = (char)toupper((unsigned char)*c); }
    void toLowerCase() { for (char *c = buf; *c; c++) *c = (char)tolower((unsigned char)*c); }

    long toInt() const { return strtol(buf, nullptr, 10); }
    float toFloat() const { return strtof(buf, nullptr); }
    double toDouble() const { return strtod(buf, nullptr); }

    void toCharArray(char *out, unsigned int len) const
    {
        strncpy(out, buf, len);
        if (len) out[len - 1] = 0;
    }
};
