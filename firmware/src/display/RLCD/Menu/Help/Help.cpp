#include "Help.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

// A cheat-sheet row. header -> inverse section bar; empty key -> spacer; empty
// action -> the key text spans the whole column (used for the letter jumps).
struct HelpLine
{
    const char *key;
    const char *action;
    bool header;
};
#define HDR(s) {s, "", true}
#define GAP {"", "", false}

// "^" stands for Ctrl (Fn on the physical keypad) - keeps the chord column
// narrow enough for a two-column layout.

// ---- EDITOR shortcuts: shown in the editor's Ctrl+/ overlay ----------------
// Grouped, not alphabetical: text edits, then the file, then the device, then
// the way out. Delete moved here from NAVIGATE - it edits, it doesn't navigate.
static const HelpLine EDIT_LEFT[] = {
    HDR("EDITING"),
    {"^Z / Y", "Undo/Redo", false},
    {"^X/C/V", "Clipboard", false},
    {"^A", "Select all", false},
    {"Del", "Delete fwd", false},
    {"^S", "Save", false},
    {"^U", "Sync file", false},
    {"^G", "AI proofread", false},
    {"^Sh+U", "Sync all", false},
    {"^Space", "Layout", false},
    {"^H", "Status bar", false},
    {"^,", "Prefs", false},
    {"^.", "Settings", false},
    {"ESC", "Open menu", false},
};
// Movement ordered by how far it takes you - char, word, paragraph, line, page,
// document - then the selection variants.
static const HelpLine EDIT_RIGHT[] = {
    HDR("NAVIGATE"),
    {"ARROWS", "Move", false},
    {"^L/R", "Word jump", false},
    {"^Up/Dn", "Paragraph", false},
    {"Home/End", "Line ends", false},
    {"PgUp/Dn", "Page", false},
    {"^Home/End", "Doc ends", false},
    {"Sh+Arrow", "Select", false},
    {"^Sh L/R", "Sel word", false},
};

// ---- MENU / FILES shortcuts: shown on the main HELP screen ------------------
// Getting around first, then what you can do to the highlighted file, with the
// destructive one last.
static const HelpLine MENU_LEFT[] = {
    HDR("FILES & MENU"),
    {"UP/DN", "Move", false},
    {"<- / ->", "Switch tab", false},
    {"ENT", "Open", false},
    {"N", "New file", false},
    {"R", "Rename", false},
    {"X", "Delete", false},
};
static const HelpLine MENU_RIGHT[] = {
    HDR("JUMP (files/settings)"),
    {"P", "Preferences", false},
    {"L", "Language", false},
    {"W", "Wi-Fi", false},
    {"S", "Sync", false},
    {"D", "USB Drive", false},
#ifdef USE_BLE_KEYBOARD_HOST
    {"K", "Keyboard", false},
#endif
    {"U", "Update", false},
    {"H", "Help", false},
    {"A", "About", false},
};

#define N(a) ((int)(sizeof(a) / sizeof(a[0])))

// A row reads action-first with its key right-aligned at the column edge, the
// way a menu lists its shortcut - the actions are what you scan for, the key is
// the answer you land on.
static void drawColumn(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                       const HelpLine *lines, int n, int x0, int xEnd, int textX,
                       int y0 = 48, int pitch = 17)
{
    for (int r = 0; r < n; r++)
    {
        int y = y0 + r * pitch;

        if (lines[r].header)
        {
            display->drawFilledRectangle(x0, y - 13, xEnd, y + 3, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
            u8->setCursor(textX, y);
            u8->print(lines[r].key);
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
            continue;
        }

        if (lines[r].key[0] == '\0')
            continue; // spacer

        if (lines[r].action[0] == '\0')
        {
            u8->setCursor(textX, y); // key-only row: it reads as the label
            u8->print(lines[r].key);
            continue;
        }

        u8->setCursor(textX, y);
        u8->print(lines[r].action);
        u8->setCursor(xEnd - u8->getUTF8Width(lines[r].key), y);
        u8->print(lines[r].key);
    }
}

void Help_render_editor(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    display->clearDisplay();
    Menu_drawHeader(display, u8, "EDITOR SHORTCUTS");

    u8->setFont(u8g2_font_profont17_tf);
    drawColumn(display, u8, EDIT_LEFT, N(EDIT_LEFT), 8, 194, 12);
    drawColumn(display, u8, EDIT_RIGHT, N(EDIT_RIGHT), 204, 396, 208);
    // column divider, ended at the last row - with no footer below there is
    // nothing to close off a longer rule
    display->drawLine(200, 40, 200, 48 + (N(EDIT_LEFT) - 1) * 17 + 6, 1);
}

void Help_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    display->clearDisplay();
}

void Help_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawHeader(display, u8, "HELP");

    u8->setFont(u8g2_font_profont17_tf);
    // Pitch is set by the JUMP column, the taller of the two: its last row has to
    // clear the callout box at y=228 (10 rows on a BLE build). Anything roomier
    // and About lands on top of the box.
    const int y0 = 54, pitch = 18;
    drawColumn(display, u8, MENU_LEFT, N(MENU_LEFT), 8, 194, 12, y0, pitch);
    drawColumn(display, u8, MENU_RIGHT, N(MENU_RIGHT), 204, 396, 208, y0, pitch);

    // full-height divider matching the columns (6 rows under the header)
    display->drawLine(200, 44, 200, y0 + 6 * pitch - 4, 1);

    // Pointer to the in-editor help, in a callout box so it stands out. The
    // caret legend rides along inside it - it belongs with the notation it
    // explains, and this screen has no footer to put it in.
    const int bx = 8, by = 228, bw = 384, bh = 56;
    RLCD_drawWindow(display, u8, bx, by, bw, bh, nullptr);
    u8->setCursor(bx + 14, by + 24);
    u8->print("Editor shortcuts: press ^/ while writing");
    u8->setCursor(bx + 14, by + 46);
    u8->print("^ = Ctrl / Fn");
}

void Help_keyboard(int key)
{
    JsonDocument &app = status();

    if (key == 27 || key == MENU || key == 18 || key == 'B' || key == 'b' || key == '\b')
    {
        app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
        return;
    }

    // The cheat-sheet lists these as "JUMP TO (any menu)", so honour them here
    // too. Keep the originating tab as the return target (don't overwrite it).
    if (key == 'P' || key == 'p')
    {
        app["menu"]["prefs_from_editor"] = false;
        app["menu"]["state"] = MENU_PREFS;
        return;
    }
    if (key == 'A' || key == 'a') { app["menu"]["state"] = MENU_ABOUT; return; }
    if (key == 'L' || key == 'l') { app["menu"]["state"] = MENU_LAYOUT; return; }
    if (key == 'W' || key == 'w') { app["menu"]["state"] = MENU_WIFI; return; }
    if (key == 'D' || key == 'd') { app["menu"]["state"] = MENU_STORAGE; return; }
#ifdef USE_BLE_KEYBOARD_HOST
    if (key == 'K' || key == 'k') { app["menu"]["state"] = MENU_BLUETOOTH; return; }
#endif
    if ((key == 'S' || key == 's') && !app["config"]["sync"]["url"].as<String>().isEmpty())
    { app["menu"]["state"] = MENU_SYNC; return; }
    if (key == 'U' || key == 'u')
    { app["menu"]["state"] = MENU_UPDATE; return; }
}
