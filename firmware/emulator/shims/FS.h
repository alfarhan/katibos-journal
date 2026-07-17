#pragma once
// Host File: backed by a stdio FILE*. Mirrors the subset of the Arduino
// fs::File API the firmware uses.
#include <cstdio>
#include <cstdint>
#include <cstddef>
#include <memory>
#include "WString.h"
#include "Print.h"

// Arduino fs open-mode macros (gfs()->open takes a stdio-style mode string).
#ifndef FILE_READ
#define FILE_READ "r"
#endif
#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif
#ifndef FILE_APPEND
#define FILE_APPEND "a"
#endif

class File : public Print
{
    std::shared_ptr<FILE> fp;

public:
    File() {}
    explicit File(FILE *f) { if (f) fp = std::shared_ptr<FILE>(f, [](FILE *p) { if (p) fclose(p); }); }

    operator bool() const { return (bool)fp; }

    int available()
    {
        if (!fp) return 0;
        long cur = ftell(fp.get());
        fseek(fp.get(), 0, SEEK_END);
        long end = ftell(fp.get());
        fseek(fp.get(), cur, SEEK_SET);
        return (int)(end - cur);
    }

    int read()
    {
        if (!fp) return -1;
        int c = fgetc(fp.get());
        return c;
    }
    size_t read(uint8_t *buf, size_t len) { return fp ? fread(buf, 1, len, fp.get()) : 0; }
    size_t readBytes(char *buf, size_t len) { return fp ? fread(buf, 1, len, fp.get()) : 0; }

    int peek()
    {
        if (!fp) return -1;
        int c = fgetc(fp.get());
        if (c != EOF) ungetc(c, fp.get());
        return c;
    }

    size_t write(uint8_t c) override { return fp ? fwrite(&c, 1, 1, fp.get()) : 0; }
    size_t write(const uint8_t *buf, size_t len) override { return fp ? fwrite(buf, 1, len, fp.get()) : 0; }
    size_t write(const char *buf, size_t len) { return fp ? fwrite(buf, 1, len, fp.get()) : 0; }
    using Print::print;
    size_t print(const char *str) { return (fp && str) ? fwrite(str, 1, strlen(str), fp.get()) : 0; }
    size_t print(const String &s) { return print(s.c_str()); }

    bool seek(uint32_t pos) { return fp ? (fseek(fp.get(), (long)pos, SEEK_SET) == 0) : false; }
    bool seek(uint32_t pos, int whence) { return fp ? (fseek(fp.get(), (long)pos, whence) == 0) : false; }

    size_t position() { return fp ? (size_t)ftell(fp.get()) : 0; }

    size_t size()
    {
        if (!fp) return 0;
        long cur = ftell(fp.get());
        fseek(fp.get(), 0, SEEK_END);
        long end = ftell(fp.get());
        fseek(fp.get(), cur, SEEK_SET);
        return (size_t)end;
    }

    String readString()
    {
        if (!fp) return String();
        long cur = ftell(fp.get());
        fseek(fp.get(), 0, SEEK_END);
        long end = ftell(fp.get());
        fseek(fp.get(), cur, SEEK_SET);
        long n = end - cur;
        if (n <= 0) return String();
        std::string out;
        out.resize(n);
        size_t got = fread(&out[0], 1, n, fp.get());
        out.resize(got);
        return String(out);
    }

    String readStringUntil(char terminator)
    {
        std::string out;
        int c;
        while (fp && (c = fgetc(fp.get())) != EOF)
        {
            if ((char)c == terminator) break;
            out += (char)c;
        }
        return String(out);
    }

    void flush() { if (fp) fflush(fp.get()); }
    void close() { fp.reset(); }
};
