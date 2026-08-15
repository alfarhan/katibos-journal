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
// Side by side: file list left, the cards stacked in a column on the right. Nine
// cards down 254px of height leaves each 28px - exactly the icon - so they sit
// edge to edge as one panel rather than nine floating windows.
static const int SPLIT_X = 238;
static const int CARD_X = 246;
static const int CARD_W = 400 - CARD_X - 8;
static const int CARD_TOP_Y = 38;

// The card block is sized from how many cards this board actually has - a BLE
// build has nine (Keyboard) and needs a fifth row - and the file list takes
// whatever is left. Hardcoding either one squashed the icons on one board or
// wasted a row on the other.
// Card height falls out of how many this board has; the list simply fills its
// own column, which is why the split no longer costs it rows.
static int Home_cardH()
{
    int ids[16];
    int n = Settings_cards(ids, 16);
    if (n < 1)
        n = 1;
    int h = (292 - CARD_TOP_Y) / n;
    return h > 34 ? 34 : h;
}
static int Home_listRows()
{
    int rows = (290 - LIST_TOP) / LIST_PITCH + 1;
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

// Longest prefix of `title` that still fits `avail` px once shaped. Measured,
// not a fixed character cap: the count column moves with the number's width, and
// an Arabic glyph is not the same width as a Latin one.
static String Home_fitTitle(U8G2_FOR_ST73XX *u8, const String &title, int avail)
{
    for (int n = 24; n > 4; n--)
    {
        String t = capUtf8(title, n);
        if (RLCD_shapedLabelWidth(u8, t.c_str(), false) <= avail)
            return t;
    }
    return capUtf8(title, 4);
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
    display->drawLine(SPLIT_X, 34, SPLIT_X, 292, 1);

    // ---- files, left. No header: the list is the subject of the screen, and the
    // ---- row a header would cost goes to a file instead.
    int page = paginate::pageOf(g_cursor, HOME_PER_PAGE);
    int rows = paginate::rowsOnPage(page, HOME_PER_PAGE, g_count);
    const int SBX = SPLIT_X - 22;

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
        u8->printf("[%d] ", idx);
        int tx = u8->getCursorX();

        char cnt[16];
        snprintf(cnt, sizeof(cnt), "%d", Home_wordCount(idx));
        int cntX = SBX - 12 - u8->getUTF8Width(cnt);
        u8->setCursor(cntX, y);
        u8->print(cnt);

        String title = app["config"][format("title_%d", idx)].as<String>();
        if (title.isEmpty() || title == "null")
            title = "(empty)";
        RLCD_drawShapedLabel(u8, tx, y, Home_fitTitle(u8, title, cntX - 6 - tx).c_str(), false);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }

    Home_drawMacScroll(display, SBX, 36, 290, page * HOME_PER_PAGE, HOME_PER_PAGE, g_count);

    // ---- cards, right: one column, edge to edge
    int ids[16];
    int n = Settings_cards(ids, 16);
    int ch = Home_cardH();
    for (int i = 0; i < n; i++)
        Settings_drawCard(display, u8, ids[i], CARD_X, CARD_TOP_Y + i * ch, CARD_W, ch,
                          g_inCards && i == g_card);
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
    // The divider is vertical now, so Right crosses to the cards and Left comes
    // back. Tab still works for anyone who learned it.
    if (!g_inCards && key == 19)
    {
        g_inCards = true;
        Menu_clear();
        return;
    }

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

        if (key == 20) { if (g_card > 0) g_card--; Menu_clear(); return; }     // Up
        if (key == 21) { if (g_card + 1 < n) g_card++; Menu_clear(); return; } // Down
        if (key == 18) { g_inCards = false; Menu_clear(); return; }            // Left: back to the files
        if (key == 19) { Menu_clear(); return; }                               // nothing right of here
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
    // T is a jump like the rest, even though it opens a confirm over this screen
    // rather than a screen of its own - Home_render/Home_keyboard already hand
    // the whole screen to that dialog while it is up.
    if (key == 'T' || key == 't')
    {
        Settings_letter(key);
        return;
    }

    // Back to the editor
    if (key == '\b' || key == 'B' || key == 'b' || key == 27 || key == MENU)
        app["screen"] = WORDPROCESSOR;
}
