#include "microtest.h"
#include "service/Tools/TextUtil.h"

// ---- deriveTitle: first non-empty line, trimmed, capped ----

TEST(title_first_line) {
    CHECK_EQ_STR(deriveTitle("Hello world\nsecond line").c_str(), "Hello world");
}

TEST(title_skips_leading_blank_lines) {
    CHECK_EQ_STR(deriveTitle("\n\n   \n\tReal line\nx").c_str(), "Real line");
}

TEST(title_trims_surrounding_space) {
    CHECK_EQ_STR(deriveTitle("   spaced title   \nnext").c_str(), "spaced title");
}

TEST(title_strips_trailing_cr) {
    CHECK_EQ_STR(deriveTitle("crlf line\r\nnext").c_str(), "crlf line");
}

TEST(title_empty_when_blank) {
    CHECK_EQ_STR(deriveTitle("").c_str(), "");
    CHECK_EQ_STR(deriveTitle("   \n\t\n  \n").c_str(), "");
}

TEST(title_caps_length) {
    // 40 'a' chars, cap at 28
    String forty = "";
    for (int i = 0; i < 40; i++) forty += 'a';
    CHECK_EQ_INT(deriveTitle(forty, 28).length(), 28);
}

TEST(title_arabic_line_preserved) {
    // first line should come back byte-for-byte (trimmed)
    CHECK_EQ_STR(deriveTitle("مرحبا بالعالم\nx").c_str(), "مرحبا بالعالم");
}

TEST(title_arabic_cap_no_split) {
    // each Arabic letter here is 2 UTF-8 bytes; cap at 3 chars => first 3 letters (6 bytes)
    // مرحبا -> م ر ح ب ا ; first 3 = مرح (valid, not a split codepoint)
    String t = deriveTitle("مرحبا", 3);
    CHECK_EQ_STR(t.c_str(), "مرح");
    // result must be whole bytes: length divisible into complete codepoints (6 bytes)
    CHECK_EQ_INT(t.length(), 6);
}

// ---- sanitizeFilename: safe ASCII filename, never empty ----

TEST(fname_passthrough) {
    CHECK_EQ_STR(sanitizeFilename("draft-essay").c_str(), "draft-essay");
}

TEST(fname_strips_illegal) {
    CHECK_EQ_STR(sanitizeFilename("my/file:name?*").c_str(), "myfilename");
}

TEST(fname_drops_dots) {
    // dots are unsafe in a filename stem (hidden files, .., extension confusion)
    CHECK_EQ_STR(sanitizeFilename("v2.0 final.").c_str(), "v20 final");
    CHECK_EQ_STR(sanitizeFilename("..hidden").c_str(), "hidden");
    CHECK_EQ_STR(sanitizeFilename(".env").c_str(), "env");
}

TEST(fname_collapses_and_trims_space) {
    CHECK_EQ_STR(sanitizeFilename("  hello   world  ").c_str(), "hello world");
}

TEST(fname_nonascii_to_untitled) {
    CHECK_EQ_STR(sanitizeFilename("مرحبا").c_str(), "untitled");
    CHECK_EQ_STR(sanitizeFilename("").c_str(), "untitled");
}

TEST(fname_no_illegal_remains) {
    String r = sanitizeFilename("a/b\\c:d*e?f\"g<h>i|j.k");
    for (unsigned i = 0; i < r.length(); i++) {
        char c = r[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '-' || c == '_' || c == ' ';
        CHECK(ok);
    }
}
