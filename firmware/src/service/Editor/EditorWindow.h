#pragma once

// Pure window math for "resume at last cursor position". Given the absolute byte
// offset `caretAbs` the caret sat at last save, the file size, and the buffer
// capacity, decide which window [seek, seek+len) to load and where the caret
// lands inside it (`cursor`). Kept dependency-free so it can be unit-tested
// without the Arduino/filesystem layer (the load path that uses it is not).
//
// The window keeps up to half a buffer of context above the caret, so the
// scrollback view fills the screen instead of opening on a near-empty page. The
// caller reads forward from `seek`, capping the read at `bufSize`.
static inline void editorResumeWindow(long caretAbs, long fileSize, long bufSize,
                                      long &seek, long &cursor)
{
    if (caretAbs < 0)
        caretAbs = 0;
    if (caretAbs > fileSize)
        caretAbs = fileSize;

    long step = bufSize / 2;

    if (fileSize <= bufSize || caretAbs <= step)
        seek = 0; // whole file fits, or caret is near the head: open at the top
    else
        seek = caretAbs - step; // keep ~half a buffer of context above the caret

    cursor = caretAbs - seek;
}
