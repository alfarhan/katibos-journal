#include "Help.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"

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
static const HelpLine EDIT_LEFT[] = {
    HDR("EDITING"),
    {"Esc", "Open menu", false},
    {"^S", "Save", false},
    {"^U", "Sync file", false},
    {"^Z / Y", "Undo/Redo", false},
    {"^X/C/V", "Clipboard", false},
    {"^A", "Select all", false},
    {"^H", "Status bar", false},
    {"^Space", "Layout", false},
    {"^D", "Date+time", false},
};
static const HelpLine EDIT_RIGHT[] = {
    HDR("NAVIGATE"),
    {"Arrows", "Move", false},
    {"^L/R", "Word jump", false},
    {"^Up/Dn", "Paragraph", false},
    {"Sh+Arrow", "Select", false},
    {"^Sh L/R", "Sel word", false},
    {"Home/End", "Line ends", false},
    {"^Home/End", "Doc ends", false},
    {"PgUp/Dn", "Page", false},
    {"Del", "Delete fwd", false},
};

// ---- MENU / FILES shortcuts: shown on the main HELP screen ------------------
static const HelpLine MENU_LEFT[] = {
    HDR("FILES & MENU"),
    {"Up/Dn", "Move", false},
    {"Enter", "Open", false},
    {"R", "Rename", false},
    {"D", "Delete", false},
    {"N", "New file", false},
    {"<- / ->", "Switch tab", false},
};
static const HelpLine MENU_RIGHT[] = {
    HDR("JUMP TO (any menu)"),
    {"L", "Language", false},
    {"W", "Wi-Fi", false},
    {"S", "Sync", false},
    {"U", "USB Drive", false},
    {"T", "Bluetooth", false},
    {"P", "Update", false},
    {"H", "Help", false},
};

#define N(a) ((int)(sizeof(a) / sizeof(a[0])))

static void drawColumn(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                       const HelpLine *lines, int n, int x0, int xEnd, int keyX, int actX,
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
            u8->setCursor(keyX, y);
            u8->print(lines[r].key);
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
            continue;
        }

        if (lines[r].key[0] == '\0')
            continue; // spacer

        u8->setCursor(keyX, y);
        u8->print(lines[r].key);
        if (lines[r].action[0] != '\0')
        {
            u8->setCursor(actX, y);
            u8->print(lines[r].action);
        }
    }
}

void Help_render_editor(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    display->clearDisplay();
    Menu_drawHeader(display, u8, "EDITOR SHORTCUTS");

    u8->setFont(u8g2_font_profont17_tf);
    drawColumn(display, u8, EDIT_LEFT, N(EDIT_LEFT), 8, 196, 12, 104);
    drawColumn(display, u8, EDIT_RIGHT, N(EDIT_RIGHT), 204, 398, 208, 300);
    display->drawLine(200, 40, 200, 268, 1);

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[Esc] back to file      ^Sh+U = sync all");
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
    // roomier rows (the menu help is short, so spread it out and balance both
    // columns one-per-row). Single-letter right keys sit close to their labels.
    const int y0 = 56, pitch = 20;
    drawColumn(display, u8, MENU_LEFT, N(MENU_LEFT), 8, 196, 12, 100, y0, pitch);
    drawColumn(display, u8, MENU_RIGHT, N(MENU_RIGHT), 204, 398, 208, 236, y0, pitch);

    // full-height divider matching the columns (6 rows under the header)
    display->drawLine(200, 44, 200, y0 + 6 * pitch - 4, 1);

    // Pointer to the in-editor help, in a callout box so it stands out.
    const int bx = 8, by = 212, bw = 384, bh = 32;
    display->drawLine(bx, by, bx + bw, by, 1);
    display->drawLine(bx, by + bh, bx + bw, by + bh, 1);
    display->drawLine(bx, by, bx, by + bh, 1);
    display->drawLine(bx + bw, by, bx + bw, by + bh, 1);
    u8->setCursor(bx + 14, by + 21);
    u8->print("Editor shortcuts: press ^/ while writing");

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(8, 296);
    u8->print("[<-] back                    ^ = Ctrl / Fn");
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
    if (key == 'L' || key == 'l') { app["menu"]["state"] = MENU_LAYOUT; return; }
    if (key == 'W' || key == 'w') { app["menu"]["state"] = MENU_WIFI; return; }
    if (key == 'U' || key == 'u') { app["menu"]["state"] = MENU_STORAGE; return; }
    if (key == 'T' || key == 't') { app["screen"] = KEYBOARDSCREEN; return; }
    if ((key == 'S' || key == 's') && !app["config"]["sync"]["url"].as<String>().isEmpty())
    { app["menu"]["state"] = MENU_SYNC; return; }
    if (key == 'P' || key == 'p')
    { app["menu"]["state"] = MENU_UPDATE; return; }
}
