#include "Home.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "keyboard/keyboard.h"

//
#include "service/Editor/Editor.h"
#include "service/Tools/TextUtil.h"
#include "service/Clock/Clock.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

#include <algorithm>

// Files are stored as /0.txt .. /N.txt. There's no directory-listing API in the
// FileSystem abstraction, so we enumerate by probing exists() up to a cap (kept
// well above any realistic journal). Cheap, safe (only existing tested APIs),
// and re-run each time the Home screen is entered.
static const int HOME_MAX_FILES = 100;
static const int HOME_PER_PAGE = 10;

static int g_indices[HOME_MAX_FILES];
static int g_count = 0;
static int g_cursor = 0;

// Full-document word count for a slot, from the cached config fields (file part
// on disk + in-memory buffer part). No file read - same number the editor's
// status bar shows.
static int Home_wordCount(int idx)
{
    JsonDocument &app = status();
    return app["config"][format("wordcount_file_%d", idx)].as<int>() +
           app["config"][format("wordcount_buffer_%d", idx)].as<int>();
}

// Relative-age label for a slot's last successful sync ("today", "3d"). Empty
// when the slot has no recorded sync day, or the clock isn't confirmed this
// session (no "today" to measure against). Lets the ✓ read as "synced N ago".
static String Home_syncAge(int idx)
{
    JsonDocument &app = status();
    int sd = app["config"][format("synced_day_%d", idx)].as<int>();
    if (sd <= 0)
        return String("");
    int today = clock_localday();
    if (today <= 0)
        return String("");
    int days = today - sd;
    if (days <= 0)
        return String("today");
    char b[8];
    snprintf(b, sizeof(b), "%dd", days > 99 ? 99 : days);
    return String(b);
}

// Per-file sync marker, drawn just left of the word count: a checkmark when the
// file is up to date on Drive, a filled dot when it has edits not yet synced
// (the unsynced flag - set on save, cleared on a successful sync). `xr` is the
// marker's right edge; `color` flips to white on the focused (inverse) row.
static void Home_drawSyncMark(ST7305_4p2_BW_DisplayDriver *display, int xr, int y, bool synced, uint16_t color)
{
    if (synced)
    {
        display->drawLine(xr - 9, y - 4, xr - 5, y, color); // checkmark
        display->drawLine(xr - 5, y, xr, y - 8, color);
    }
    else
    {
        display->drawFilledRectangle(xr - 7, y - 7, xr - 2, y - 2, color); // pending dot
    }
}

static void Home_enumerate()
{
    g_count = 0;
    for (int i = 0; i < HOME_MAX_FILES && g_count < HOME_MAX_FILES; i++)
    {
        if (gfs()->exists(format("/%d.txt", i).c_str()))
            g_indices[g_count++] = i;
    }

    // Order by most-recently edited (largest edit_seq first); files never edited
    // this config (seq 0) fall to the bottom keeping their numeric order.
    JsonDocument &app = status();
    std::stable_sort(g_indices, g_indices + g_count, [&](int a, int b) {
        int ea = app["config"][format("edited_%d", a)].as<int>();
        int eb = app["config"][format("edited_%d", b)].as<int>();
        return ea > eb;
    });
}

// Make `fileIndex` the editor's current file. Operations like delete/rename act
// on Editor::fileName, so the focused slot MUST be loaded first - otherwise they
// would target whatever file was previously open (the "deleted the wrong file"
// bug).
static void Home_select(int fileIndex)
{
    JsonDocument &app = status();
    app["config"]["file_index"] = fileIndex;
    config_save();
    Editor::getInstance().loadFile(format("/%d.txt", fileIndex));
}

static void Home_open(int fileIndex)
{
    Home_select(fileIndex);
    status()["screen"] = WORDPROCESSOR;
}

//
void Home_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();

    JsonDocument &app = status();
    app["menu"]["state"] = MENU_HOME;

    // refresh the file list and put the cursor on the active file
    Home_enumerate();
    int fi = app["config"]["file_index"].as<int>();
    g_cursor = 0;
    for (int r = 0; r < g_count; r++)
        if (g_indices[r] == fi) { g_cursor = r; break; }
}

//
void Home_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // ---- paged, cursor-driven file list (full width) ----
    int pos_x = 10;
    int page = paginate::pageOf(g_cursor, HOME_PER_PAGE);
    int rows = paginate::rowsOnPage(page, HOME_PER_PAGE, g_count);
    int pages = paginate::pageCount(g_count, HOME_PER_PAGE);

    Menu_drawTabs(display, u8, 0);


    for (int r = 0; r < rows; r++)
    {
        int idx = g_indices[page * HOME_PER_PAGE + r];
        int y = 52 + r * 20;
        bool focused = (page * HOME_PER_PAGE + r == g_cursor);

        if (focused)
        {
            display->drawFilledRectangle(pos_x - 2, y - 15, 399, y + 4, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        // Number first, on the left (LTR), then the name. Drawn as two pieces so
        // an Arabic title never reorders the "[N]" to the right - the prefix is
        // plain LTR text, the title follows it (shaped, RTL within itself).
        u8->setFont(u8g2_font_profont17_tf);
        u8->setCursor(pos_x, y);
        u8->printf("[%d]  ", idx);
        int tx = u8->getCursorX();

        // full-document word count, right-aligned (drawn first so the title can
        // be capped to whatever space is left of it)
        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d w", Home_wordCount(idx));
        int cntX = 392 - u8->getUTF8Width(cnt);
        u8->setCursor(cntX, y);
        u8->print(cnt);

        // sync marker just left of the word count
        bool synced = !app["config"][format("unsynced_%d", idx)].as<bool>();
        Home_drawSyncMark(display, cntX - 6, y, synced,
                          focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);

        // for synced files, show how long ago (right-aligned, left of the mark)
        if (synced)
        {
            String age = Home_syncAge(idx);
            if (!age.isEmpty())
            {
                int ageX = (cntX - 15) - 4 - u8->getUTF8Width(age.c_str());
                u8->setCursor(ageX, y);
                u8->print(age.c_str());
            }
        }

        String title = app["config"][format("title_%d", idx)].as<String>();
        if (title.isEmpty() || title == "null")
        {
            u8->setCursor(tx, y);
            u8->print("-");
        }
        else
        {
            RLCD_drawShapedLabel(u8, tx, y, capUtf8(title, 16).c_str(), false);
        }

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    // divider above the file-action hints, then the hints on one line. The
    // divider sits ~20px above the text baseline so the whitespace below the
    // line matches the whitespace below the header title (header: text 18,
    // line 28 -> ~10px of visible gap).
    display->drawLine(0, 276, 400, 276, 1);
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(pos_x, 296);
    u8->print("Enter | Rename | Delete | New");

    // page indicator in the footer (right), clear of the header date+time
    if (pages > 1)
    {
        char pg[12];
        snprintf(pg, sizeof(pg), "p%d/%d", page + 1, pages);
        u8->setCursor(392 - u8->getUTF8Width(pg), 296);
        u8->print(pg);
    }
}

//
void Home_keyboard(char key)
{
    JsonDocument &app = status();

    // ---- file-list navigation (arrow/page codes are layout-independent) ----
    if (key == 20) // Up
    {
        g_cursor = paginate::clampInt(g_cursor - 1, 0, g_count > 0 ? g_count - 1 : 0);
        Menu_clear();
        return;
    }
    if (key == 21) // Down
    {
        g_cursor = paginate::clampInt(g_cursor + 1, 0, g_count > 0 ? g_count - 1 : 0);
        Menu_clear();
        return;
    }
    if (key == 22) // Page Up
    {
        g_cursor = paginate::clampInt(g_cursor - HOME_PER_PAGE, 0, g_count > 0 ? g_count - 1 : 0);
        Menu_clear();
        return;
    }
    if (key == 23) // Page Down
    {
        g_cursor = paginate::clampInt(g_cursor + HOME_PER_PAGE, 0, g_count > 0 ? g_count - 1 : 0);
        Menu_clear();
        return;
    }

    // Enter: open the focused file
    if ((key == '\n' || key == '\r') && g_count > 0)
    {
        Home_open(g_indices[g_cursor]);
        return;
    }

    // Digit: jump to + open that visible row on the current page (page-relative)
    if (key > 47 && key < 58)
    {
        int page = paginate::pageOf(g_cursor, HOME_PER_PAGE);
        int rows = paginate::rowsOnPage(page, HOME_PER_PAGE, g_count);
        int row = key - 48;
        if (row < rows)
            Home_open(g_indices[page * HOME_PER_PAGE + row]);
        return;
    }

    // Rename the focused file (load it first so Rename acts on the right file)
    if (key == 'R' || key == 'r')
    {
        if (g_count > 0)
        {
            Home_select(g_indices[g_cursor]);
            app["menu"]["state"] = MENU_RENAME;
        }
        return;
    }

    // Delete the focused file (load it first so deleteFile targets the right
    // file) - the confirmation screen performs the deletion
    if (key == 'D' || key == 'd')
    {
        if (g_count > 0)
        {
            Home_select(g_indices[g_cursor]);
            app["menu"]["state"] = MENU_CLEAR;
        }
        return;
    }

    // New file: open the lowest free slot (loadFile creates it)
    if (key == 'N' || key == 'n')
    {
        for (int i = 0; i < HOME_MAX_FILES; i++)
        {
            if (!gfs()->exists(format("/%d.txt", i).c_str()))
            {
                Home_open(i);
                return;
            }
        }
        return;
    }

    // Right arrow switches to the SETTINGS tab
    if (key == 19)
    {
        app["menu"]["state"] = MENU_SETTINGS;
        Menu_clear();
        return;
    }

    // Settings fast-paths, reachable straight from the file list (same letters
    // as the SETTINGS tab) so a setting is one key away from anywhere in the menu.
    // Record FILES as the tab to return to, so Esc lands back here, not Settings.
    if (key == 'L' || key == 'l') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_LAYOUT; return; }
    if (key == 'W' || key == 'w') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_WIFI; return; }
    if (key == 'U' || key == 'u') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_STORAGE; return; }
    if (key == 'T' || key == 't') { app["screen"] = KEYBOARDSCREEN; return; }
    if (key == 'H' || key == 'h') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_HELP; return; }
    if ((key == 'S' || key == 's') && !app["config"]["sync"]["url"].as<String>().isEmpty())
    {
        app["menu"]["return"] = MENU_HOME;
        app["menu"]["state"] = MENU_SYNC;
        return;
    }
    if (key == 'P' || key == 'p')
    {
        app["menu"]["return"] = MENU_HOME;
        app["menu"]["state"] = MENU_UPDATE;
        return;
    }

    // Back to the editor
    if (key == '\b' || key == 'B' || key == 'b' || key == 27 || key == MENU)
        app["screen"] = WORDPROCESSOR;
}
