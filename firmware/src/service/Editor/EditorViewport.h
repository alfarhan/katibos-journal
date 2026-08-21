#pragma once
// Pure viewport math: where the active (caret) line sits on screen and which
// file line is at the top of the view. Dependency-free so it can be unit-tested
// (tests/test_editorviewport.cpp) and reused by the renderer.
//
// caretRow = cursorLine - topLine; the renderer draws screen row r as file line
// (topLine + r). `lastRow` is the bottom-most usable screen row (visibleRows-1).

enum
{
    SCROLL_TOP = 0,    // normal editor: caret floats; the view pages at the edges
    SCROLL_MIDDLE = 1, // caret pinned to the vertical middle (typewriter)
    SCROLL_BOTTOM = 2, // caret pinned to the bottom edge (legacy default)
};

// Sentinel for "no window yet" - the first render after a file loads bottom-
// anchors on the caret exactly as it always did.
#define EDITOR_TOP_UNSET (-1000000)

// First visible (top) file line for the given mode. Fixed modes derive it from
// the caret alone (may go negative near the file head → blank rows above, which
// matches the legacy bottom-anchored feel). SCROLL_TOP pages off prevTopLine so
// the caret only forces a scroll once it leaves [topLine, topLine+lastRow].
static inline int editorTopLine(int mode, int cursorLine, int lastRow, int prevTopLine,
                               int totalLine)
{
    if (mode == SCROLL_BOTTOM)
    {
        // Appending (or arriving from below): the caret takes the bottom row, so
        // new text lands where the writer is looking. That used to be the whole
        // rule, which meant moving the caret UP scrolled the document down and
        // the line just written dropped off the bottom of the screen - nothing
        // below the caret was ever visible. Hold the window instead once the
        // caret is inside it; it only scrolls when the caret actually leaves.
        int pinned = cursorLine - lastRow;
        if (prevTopLine == EDITOR_TOP_UNSET || cursorLine >= totalLine)
            return pinned;
        if (pinned > prevTopLine)   // caret moved below the window
            return pinned;
        if (cursorLine < prevTopLine) // caret moved above it
            return cursorLine;
        return prevTopLine;
    }
    if (mode == SCROLL_MIDDLE)
        return cursorLine - lastRow / 2;

    // SCROLL_TOP
    int top = prevTopLine;
    if (cursorLine < top)
        top = cursorLine;
    else if (cursorLine > top + lastRow)
        top = cursorLine - lastRow;
    if (top < 0)
        top = 0;
    return top;
}
