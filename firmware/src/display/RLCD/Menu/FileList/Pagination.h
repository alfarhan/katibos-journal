#pragma once

// Pure pagination math for a cursor-driven, paged list. No dependencies, so it
// is unit-tested on host (tests/test_pagination.cpp).
namespace paginate
{
    inline int clampInt(int v, int lo, int hi)
    {
        return v < lo ? lo : (v > hi ? hi : v);
    }

    // Number of pages needed for `total` items at `perPage` (>= 1).
    inline int pageCount(int total, int perPage)
    {
        if (perPage <= 0) return 1;
        if (total <= 0) return 1;
        return (total + perPage - 1) / perPage;
    }

    // Which page (0-based) holds `cursor`.
    inline int pageOf(int cursor, int perPage)
    {
        if (perPage <= 0 || cursor < 0) return 0;
        return cursor / perPage;
    }

    // First item index of `page`.
    inline int pageStart(int page, int perPage)
    {
        return page * perPage;
    }

    // How many rows are visible on `page` (the last page may be short).
    inline int rowsOnPage(int page, int perPage, int total)
    {
        int rem = total - page * perPage;
        if (rem < 0) rem = 0;
        if (rem > perPage) rem = perPage;
        return rem;
    }

    // Absolute item index for visible `row` (0-based) on the page holding `cursor`.
    inline int rowToIndex(int cursor, int perPage, int row)
    {
        return pageOf(cursor, perPage) * perPage + row;
    }
}
