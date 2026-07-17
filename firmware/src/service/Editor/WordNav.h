#pragma once

// Pure caret-motion helpers, shared by word-jump (Ctrl+Left/Right), word-select
// (Ctrl+Shift+Left/Right) and paragraph-jump (Ctrl+Up/Down). Each returns the
// new caret byte offset.
//
// Boundaries are ASCII space (0x20) and newline (0x0A). Neither byte can appear
// inside a UTF-8 multi-byte sequence (lead bytes are >= 0xC0, continuation bytes
// are 0x80-0xBF), so stepping byte-by-byte always lands on a character boundary
// — the same logic is correct for Arabic and English.
static inline int editorWordJump(const char *buf, int bufLen, int pos, bool right)
{
    if (pos < 0)
        pos = 0;
    if (pos > bufLen)
        pos = bufLen;

    if (right)
    {
        // skip the rest of the current word, then the spaces after it, landing
        // on the start of the next word
        while (pos < bufLen && buf[pos] != ' ' && buf[pos] != '\n')
            pos++;
        while (pos < bufLen && buf[pos] == ' ')
            pos++;
    }
    else
    {
        // skip any spaces to the left, then the word, landing on its start
        while (pos > 0 && buf[pos - 1] == ' ')
            pos--;
        while (pos > 0 && buf[pos - 1] != ' ' && buf[pos - 1] != '\n')
            pos--;
    }
    return pos;
}

// Paragraph jump (Ctrl+Up/Down). Returns the new caret byte offset after moving
// to the start of the next (down) / current-or-previous (up) paragraph. A
// paragraph break is one or more newlines; the same '\n'-only logic is correct
// for Arabic and English (a newline never appears inside a UTF-8 sequence).
static inline int editorParagraphJump(const char *buf, int bufLen, int pos, bool down)
{
    if (pos < 0)
        pos = 0;
    if (pos > bufLen)
        pos = bufLen;

    if (down)
    {
        // to the end of the current line, then past the break (and any blank
        // lines), landing on the start of the next paragraph
        while (pos < bufLen && buf[pos] != '\n')
            pos++;
        while (pos < bufLen && buf[pos] == '\n')
            pos++;
    }
    else
    {
        // back over any break/blank lines, then to the start of this paragraph
        while (pos > 0 && buf[pos - 1] == '\n')
            pos--;
        while (pos > 0 && buf[pos - 1] != '\n')
            pos--;
    }
    return pos;
}
