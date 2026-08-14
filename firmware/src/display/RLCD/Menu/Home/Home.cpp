#include "Home.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "keyboard/keyboard.h"

//
#include "service/Editor/Editor.h"
#include "service/Tools/TextUtil.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "display/RLCD/Menu/Settings/Settings.h"

#include <algorithm>

// Files are stored as /0.txt .. /N.txt. There's no directory-listing API in the
// FileSystem abstraction, so we enumerate by probing exists() up to a cap (kept
// well above any realistic journal). Cheap, safe (only existing tested APIs),
// and re-run each time the Home screen is entered.
static const int HOME_MAX_FILES = 100;
// The list shares the screen with the settings cards now, so a "page" is what
// fits above the divider rather than the whole panel.
#define HOME_PER_PAGE Home_listRows()

// Geometry of the combined screen. Cards are 3 across because "Preferences" is
// 98px and a 4-across card would only be 91 - measured, not guessed.
static const int LIST_TOP = 52;   // first row baseline
static const int LIST_PITCH = 22;
static const int CARD_COLS = 2;   // wide cards: mark left, name right
static const int CARD_H = 32;     // enough to clear the 28px icon
static const int CARD_GAP = 4;

// The card block is sized from how many cards this board actually has - a BLE
// build has nine (Keyboard) and needs a fifth row - and the file list takes
// whatever is left. Hardcoding either one squashed the icons on one board or
// wasted a row on the other.
static int Home_cardRows()
{
    int ids[16];
    int n = Settings_cards(ids, 16);
    return (n + CARD_COLS - 1) / CARD_COLS;
}
static int Home_cardTop() { return 292 - (Home_cardRows() * CARD_H + (Home_cardRows() - 1) * CARD_GAP); }
static int Home_dividerY() { return Home_cardTop() - 8; }
static int Home_listRows()
{
    int rows = (Home_dividerY() - 6 - LIST_TOP) / LIST_PITCH + 1;
    return rows < 1 ? 1 : rows;
}

// Which half has the cursor. Tab moves between them; the arrows stay inside.
static bool g_inCards = false;
static int g_card = 0;

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

// Classic Mac scroll bar: a stippled track between two arrow boxes, with a plain
// thumb sized to the visible fraction. The 50/50 checkerboard reads as grey on
// this panel, which is how it read on a compact Mac.
static void Home_drawMacScroll(ST7305_4p2_BW_DisplayDriver *display,
                               int x, int top, int bottom, int first, int visible, int total)
{
    const int W = 16;
    display->drawRectangle(x, top, x + W, bottom, 1);
    display->drawLine(x, top + W, x + W, top + W, 1);
    display->drawLine(x, bottom - W, x + W, bottom - W, 1);
    display->drawFilledTriangle(x + 4, top + 11, x + 12, top + 11, x + 8, top + 5, 1);
    display->drawFilledTriangle(x + 4, bottom - 11, x + 12, bottom - 11, x + 8, bottom - 5, 1);

    int tTop = top + W + 1, tBot = bottom - W - 1;
    for (int y = tTop; y < tBot; y++)
        for (int px = x + 1; px < x + W; px++)
            if (((px + y) & 1) == 0)
                display->writePoint(px, y, true);

    if (total <= visible)
        return; // everything fits: track only, no thumb, like the Mac did

    int span = tBot - tTop;
    int th = span * visible / total;
    if (th < 14)
        th = 14;
    int ty = tTop + (span - th) * first / (total - visible);
    display->drawFilledRectangle(x + 1, ty, x + W - 1, ty + th, 0);
    display->drawRectangle(x + 1, ty, x + W - 1, ty + th, 1);
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
void Home_focusCards(bool on)
{
    g_inCards = on;
    if (on)
        g_card = 0;
}

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

    // The restart confirm belongs to the cards and takes the whole screen.
    if (Settings_confirmActive())
    {
        Settings_drawConfirm(display, u8);
        return;
    }

    RLCD_drawTitleBar(display, u8, 0, 0, 400, 28, "katibOS  \u0643\u0627\u062a\u0628", 0);

    // ---- files, above the rule. No header: the list is the screen's subject,
    // ---- and the row saved goes to a file instead of a label.
    int page = paginate::pageOf(g_cursor, HOME_PER_PAGE);
    int rows = paginate::rowsOnPage(page, HOME_PER_PAGE, g_count);
    const int SBX = 378;

    u8->setFont(u8g2_font_profont17_tf);
    for (int r = 0; r < rows; r++)
    {
        int idx = g_indices[page * HOME_PER_PAGE + r];
        int y = LIST_TOP + r * LIST_PITCH;
        bool focused = (!g_inCards && page * HOME_PER_PAGE + r == g_cursor);

        if (focused)
        {
            display->drawFilledRectangle(6, y - 15, SBX - 6, y + 4, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        u8->setCursor(10, y);
        u8->printf("[%d]  ", idx);
        int tx = u8->getCursorX();

        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d w", Home_wordCount(idx));
        int cntX = SBX - 12 - u8->getUTF8Width(cnt);
        u8->setCursor(cntX, y);
        u8->print(cnt);

        bool synced = !app["config"][format("unsynced_%d", idx)].as<bool>();
        Home_drawSyncMark(display, cntX - 6, y, synced,
                          focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK);

        String title = app["config"][format("title_%d", idx)].as<String>();
        if (title.isEmpty() || title == "null")
            title = "(empty)";
        RLCD_drawShapedLabel(u8, tx, y, capUtf8(title, 24).c_str(), false);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    Home_drawMacScroll(display, SBX, 36, Home_dividerY() - 6,
                       page * HOME_PER_PAGE, HOME_PER_PAGE, g_count);

    display->drawLine(0, Home_dividerY(), 400, Home_dividerY(), 1);

    // ---- the same cards the Settings tab drew, three across
    int ids[16];
    int n = Settings_cards(ids, 16);
    const int MX = 8, GAP = 6;
    int cw = (400 - 2 * MX - (CARD_COLS - 1) * GAP) / CARD_COLS;
    int ch = CARD_H;

    for (int i = 0; i < n; i++)
    {
        int cx = MX + (i % CARD_COLS) * (cw + GAP);
        int cy = Home_cardTop() + (i / CARD_COLS) * (ch + CARD_GAP);
        Settings_drawCard(display, u8, ids[i], cx, cy, cw, ch, g_inCards && i == g_card);
    }
}

void Home_keyboard(char key)
{
    JsonDocument &app0 = status();

    // The restart confirm owns every key while it is up.
    if (Settings_confirmActive())
    {
        Settings_confirmKey(key);
        return;
    }

    // Tab moves between the two halves; the arrows then stay inside whichever
    // half has the cursor, so neither list ever "escapes" under an arrow press.
    if (key == '\t' || key == 9)
    {
        g_inCards = !g_inCards;
        if (g_inCards)
        {
            int ids[16];
            int n = Settings_cards(ids, 16);
            if (g_card >= n)
                g_card = n - 1;
        }
        Menu_clear();
        return;
    }

    // ---- cards half ----
    if (g_inCards)
    {
        int ids[16];
        int n = Settings_cards(ids, 16);
        if (n <= 0)
            return;
        if (g_card >= n)
            g_card = n - 1;

        if (key == 20) { if (g_card >= CARD_COLS) g_card -= CARD_COLS; Menu_clear(); return; }
        if (key == 21) { if (g_card + CARD_COLS < n) g_card += CARD_COLS; Menu_clear(); return; }
        if (key == 19) { if (g_card + 1 < n) g_card++; Menu_clear(); return; }
        if (key == 18) { if (g_card > 0) g_card--; Menu_clear(); return; }
        if (key == '\n' || key == '\r') { Settings_openCard(ids[g_card]); return; }
        if (key == 27 || key == MENU) { app0["screen"] = WORDPROCESSOR; return; }
        if (Settings_letter(key))
            return;
        return;
    }

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

    // Delete the focused file with X (load it first so deleteFile targets the
    // right file) - the confirmation screen performs the deletion. X rather
    // than D leaves that letter for the USB Drive jump, which then works in
    // every menu including this one.
    if (key == 'X' || key == 'x')
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

    // Left/Right no longer switch tabs - there is one screen now, and Tab is
    // what crosses the divider.

    // Settings fast-paths, reachable straight from the file list (same letters
    // as the SETTINGS tab) so a setting is one key away from anywhere in the menu.
    // Record FILES as the tab to return to, so Esc lands back here, not Settings.
    if (key == 'L' || key == 'l') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_LAYOUT; return; }
    if (key == 'W' || key == 'w') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_WIFI; return; }
    if (key == 'D' || key == 'd') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_STORAGE; return; }
    if (key == 'H' || key == 'h') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_HELP; return; }
    if (key == 'A' || key == 'a') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_ABOUT; return; }
#ifdef USE_BLE_KEYBOARD_HOST
    if (key == 'K' || key == 'k') { app["menu"]["return"] = MENU_HOME; app["menu"]["state"] = MENU_BLUETOOTH; return; }
#endif
    if (key == 'P' || key == 'p')
    {
        app["menu"]["return"] = MENU_HOME;
        app["menu"]["prefs_from_editor"] = false; // Esc lands back on the file list
        app["menu"]["state"] = MENU_PREFS;
        return;
    }
    if ((key == 'S' || key == 's') &&
        (!app["config"]["sync"]["url"].as<String>().isEmpty() ||
         app["config"]["sync"]["provider"].as<String>() == "git"))
    {
        app["menu"]["return"] = MENU_HOME;
        app["menu"]["state"] = MENU_SYNC;
        return;
    }
    if (key == 'U' || key == 'u')
    {
        app["menu"]["return"] = MENU_HOME;
        app["menu"]["state"] = MENU_UPDATE;
        return;
    }

    // Back to the editor
    if (key == '\b' || key == 'B' || key == 'b' || key == 27 || key == MENU)
        app["screen"] = WORDPROCESSOR;
}
