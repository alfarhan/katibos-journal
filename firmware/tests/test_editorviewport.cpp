#include "microtest.h"
#include "service/Editor/EditorViewport.h"

// lastRow = 12 (the editor's bottom-most usable screen row on rev_8).
// caretRow = cursorLine - topLine; the renderer draws screen row r as line top+r.

// ---- BOTTOM: caret pinned to the bottom edge (legacy default) --------------
TEST(bottom_caret_always_at_last_row)
{
    CHECK_EQ_INT(editorTopLine(SCROLL_BOTTOM, 20, 12, 0), 8);   // 20-12
    CHECK_EQ_INT(20 - editorTopLine(SCROLL_BOTTOM, 20, 12, 0), 12); // caretRow
}

TEST(bottom_near_head_blank_above)
{
    // fresh/short file: caret sits at the bottom, rows above are off-screen
    CHECK_EQ_INT(editorTopLine(SCROLL_BOTTOM, 0, 12, 0), -12);
    CHECK_EQ_INT(0 - editorTopLine(SCROLL_BOTTOM, 0, 12, 0), 12); // caretRow still 12
}

// ---- MIDDLE: caret pinned to the vertical middle --------------------------
TEST(middle_caret_at_half)
{
    CHECK_EQ_INT(editorTopLine(SCROLL_MIDDLE, 30, 12, 0), 24);  // 30 - 6
    CHECK_EQ_INT(30 - editorTopLine(SCROLL_MIDDLE, 30, 12, 0), 6);
}

// ---- TOP: normal editor — caret floats, view pages at the edges -----------
TEST(top_caret_floats_from_head)
{
    // within the first page the view stays put and the caret moves down
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 0, 12, 0), 0);
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 5, 12, 0), 0);   // caretRow 5
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 12, 12, 0), 0);  // caretRow 12 (bottom)
}

TEST(top_pages_down_when_caret_passes_bottom)
{
    // caret one past the bottom row → scroll so caret rides the bottom edge
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 13, 12, 0), 1);
    CHECK_EQ_INT(13 - editorTopLine(SCROLL_TOP, 13, 12, 0), 12);
}

TEST(top_pages_up_when_caret_above_view)
{
    // caret jumped above the current view (e.g. Ctrl+Home) → top follows caret
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 3, 12, 10), 3);
}

TEST(top_holds_when_caret_inside_view)
{
    // caret still inside [top, top+12] → view does not move
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 15, 12, 10), 10); // caretRow 5
}

TEST(top_never_negative)
{
    CHECK_EQ_INT(editorTopLine(SCROLL_TOP, 2, 12, 5), 2);
    CHECK(editorTopLine(SCROLL_TOP, 0, 12, 0) >= 0);
}
