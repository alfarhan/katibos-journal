#pragma once
#include <Arduino.h>

// Pure, hardware-free text helpers (unit-tested in tests/test_textutil.cpp).

// First non-empty line of `text`, trimmed, capped to `maxChars` codepoints
// (never splits a UTF-8 sequence). Returns "" if there is no non-blank line.
String deriveTitle(const String &text, int maxChars = 28);

// Safe filename stem from a (possibly Arabic/odd) title: keeps [A-Za-z0-9-_ space]
// only (no dots — avoids hidden files / ".." / extension confusion), collapses
// and trims whitespace, caps length. Never empty — falls back to "untitled".
// The caller appends the ".txt" extension itself.
String sanitizeFilename(const String &in);

// Substring of `s` limited to `maxChars` UTF-8 codepoints (no mid-sequence cut).
String capUtf8(const String &s, int maxChars);
