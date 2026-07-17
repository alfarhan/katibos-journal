#include "TextUtil.h"

static bool isWs(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

static int utf8Len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1; // lone continuation / invalid: treat as 1 so we always advance
}

String capUtf8(const String &s, int maxChars) {
    if (maxChars <= 0) return String("");
    int n = (int)s.length();
    int i = 0, chars = 0;
    while (i < n && chars < maxChars) {
        int len = utf8Len((unsigned char)s[i]);
        if (i + len > n) len = n - i; // truncated trailing bytes: stop cleanly
        i += len;
        chars++;
    }
    return s.substring(0, i);
}

String deriveTitle(const String &text, int maxChars) {
    int n = (int)text.length();
    int i = 0;
    while (i < n) {
        int start = i;
        while (i < n && text[i] != '\n') i++;
        // line is [start, i)
        bool hasContent = false;
        for (int k = start; k < i; k++) {
            if (!isWs(text[k])) { hasContent = true; break; }
        }
        if (hasContent) {
            String line = text.substring(start, i);
            line.trim();
            return capUtf8(line, maxChars);
        }
        i++; // skip the '\n'
    }
    return String("");
}

String sanitizeFilename(const String &in) {
    String out = "";
    int n = (int)in.length();
    bool pendingSpace = false;
    for (int i = 0; i < n; i++) {
        char c = in[i];
        // Conservative whitelist: letters, digits, space, dash, underscore.
        // Dots are intentionally excluded (hidden files, "..", extension confusion).
        bool keep = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (keep) {
            if (pendingSpace && out.length() > 0) out += ' ';
            pendingSpace = false;
            out += c;
        } else if (c == ' ' || c == '\t') {
            pendingSpace = true; // collapse runs; only emitted before next kept char
        }
        // everything else (slashes, punctuation, non-ASCII) is dropped
    }
    out.trim();
    if (out.isEmpty()) return String("untitled");
    if ((int)out.length() > 48) { out = out.substring(0, 48); out.trim(); }
    return out;
}
