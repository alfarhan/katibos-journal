#include "microtest.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

using namespace paginate;

TEST(page_count) {
    CHECK_EQ_INT(pageCount(0, 10), 1);   // empty still has one (empty) page
    CHECK_EQ_INT(pageCount(1, 10), 1);
    CHECK_EQ_INT(pageCount(10, 10), 1);
    CHECK_EQ_INT(pageCount(11, 10), 2);
    CHECK_EQ_INT(pageCount(25, 10), 3);
}

TEST(page_of_cursor) {
    CHECK_EQ_INT(pageOf(0, 10), 0);
    CHECK_EQ_INT(pageOf(9, 10), 0);
    CHECK_EQ_INT(pageOf(10, 10), 1);
    CHECK_EQ_INT(pageOf(24, 10), 2);
}

TEST(rows_on_page) {
    CHECK_EQ_INT(rowsOnPage(0, 10, 25), 10); // full page
    CHECK_EQ_INT(rowsOnPage(2, 10, 25), 5);  // last page is short
    CHECK_EQ_INT(rowsOnPage(3, 10, 25), 0);  // beyond the data
    CHECK_EQ_INT(rowsOnPage(0, 10, 0), 0);
}

TEST(row_to_index) {
    CHECK_EQ_INT(rowToIndex(0, 10, 3), 3);    // page 0, row 3 -> 3
    CHECK_EQ_INT(rowToIndex(15, 10, 2), 12);  // cursor on page 1, row 2 -> 12
}

TEST(clamp) {
    CHECK_EQ_INT(clampInt(5, 0, 9), 5);
    CHECK_EQ_INT(clampInt(-1, 0, 9), 0);
    CHECK_EQ_INT(clampInt(99, 0, 9), 9);
}
