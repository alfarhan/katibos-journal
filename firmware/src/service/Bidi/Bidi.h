#pragma once

#include <stdint.h>

// Arabic shaping + bidirectional line layout for a monospace text grid.
//
// The editor stores text as UTF-8. This module turns one logical line of
// UTF-8 bytes into a left-to-right list of "cells" ready to draw: Arabic is
// shaped into its connected presentation forms, and runs are reordered so an
// RTL line reads right-to-left with embedded Latin/number runs kept LTR.
//
// Pure logic, no Arduino/display dependency, so it is unit-testable on host.

namespace bidi
{
    // One glyph to draw, in visual (left-to-right) order.
    struct Cell
    {
        uint16_t glyph;     // codepoint to render (presentation form for Arabic)
        bool arabic;        // true -> use the Arabic font, false -> Latin font
        int byteStart;      // offset of the source char(s) within the line bytes
        int byteLen;        // bytes consumed (incl. any folded combining marks)
        uint16_t marks[3];  // combining harakat to overlay on this glyph (0 advance)
        uint8_t nmarks;
    };

    // ---- UTF-8 helpers ----
    // Bytes occupied by the UTF-8 char beginning with `lead` (1..3). Lenient:
    // a stray continuation/invalid byte counts as 1.
    int utf8CharLen(uint8_t lead);
    // Encode `cp` (<= 0xFFFF) to UTF-8 in `out` (needs 3 bytes). Returns length.
    int utf8Encode(uint16_t cp, char *out);
    // Decode one UTF-8 char at `s` of at most `byteLen` bytes into `cp`.
    // Returns bytes consumed. Lenient: invalid -> cp = raw byte, length 1.
    int utf8DecodeOne(const char *s, int byteLen, uint16_t *cp);

    // ---- classification ----
    bool isArabic(uint16_t cp);       // base block or presentation forms
    bool isArabicLetter(uint16_t cp); // a joining letter in 0x0621..0x064A

    // ---- line layout ----
    // Lay out `byteLen` bytes of UTF-8 at `line` into visual-order cells.
    // Writes up to `maxCells` cells, returns the count. `*lineIsRTL` is set to
    // the resolved base direction. Base direction follows the first strong
    // character (Unicode P2/P3); `baseHintRTL` is the fallback when the line
    // has no strong character (e.g. blank, or digits/punctuation only).
    int layoutLine(const char *line, int byteLen, Cell *out, int maxCells,
                   bool *lineIsRTL, bool baseHintRTL = false);
}
