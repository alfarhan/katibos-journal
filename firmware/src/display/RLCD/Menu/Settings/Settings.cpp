#include "Settings.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

enum
{
    ACT_PREFS,
    ACT_LANGUAGE,
    ACT_WIFI,
    ACT_SYNC,
    ACT_SYNCPROV,
#ifdef USE_BLE_KEYBOARD_HOST
    ACT_BTKB,
#endif
    ACT_DRIVE,
    ACT_UPDATE,
    ACT_HELP,
    ACT_ABOUT
};

static int g_cursor = 0;

// Build the visible action list (Sync only appears when a sync URL is set), so
// render and keyboard agree on what each cursor row means.
static int buildList(int *ids)
{
    JsonDocument &app = status();
    int n = 0;
    // Language, Time zone and Sync provider live in Preferences; About is a card
    // of its own here (it's a destination, not a setting).
    ids[n++] = ACT_PREFS;
    ids[n++] = ACT_WIFI;
    // Listed when a Drive URL is set OR the provider is git (git syncs via
    // config.sync.git and leaves sync.url empty).
    if (!app["config"]["sync"]["url"].as<String>().isEmpty() ||
        app["config"]["sync"]["provider"].as<String>() == "git")
        ids[n++] = ACT_SYNC;
#ifdef USE_BLE_KEYBOARD_HOST
    ids[n++] = ACT_BTKB;
#endif
    ids[n++] = ACT_DRIVE;
    ids[n++] = ACT_UPDATE; // always available via built-in fallback URL
    ids[n++] = ACT_HELP;
    ids[n++] = ACT_ABOUT;
    return n;
}

// Card labels: one word where possible. A card is a third of the screen wide,
// so the long forms ("Check for Update") don't fit - the icon carries the rest
// of the meaning.
static const char *actionLabel(int act)
{
    switch (act)
    {
    case ACT_PREFS: return "Preferences";
    case ACT_LANGUAGE: return "Language";
    case ACT_WIFI: return "Wi-Fi";
    case ACT_SYNC: return "Sync";
    case ACT_SYNCPROV: return "Provider";
#ifdef USE_BLE_KEYBOARD_HOST
    case ACT_BTKB: return "Keyboard";
#endif
    case ACT_DRIVE: return "USB Drive";
    case ACT_UPDATE: return "Update";
    case ACT_HELP: return "Help";
    case ACT_ABOUT: return "About";
    }
    return "";
}

// The single-key jump for a card (Settings_keyboard's fast-paths). It is shown by
// underlining that letter in the card's own name rather than printing the key
// separately - the name already contains it, so the shortcut gets learned by
// reading the label. Every key here MUST appear in its label (case-insensitive).
static char actionKey(int id)
{
    switch (id)
    {
    case ACT_PREFS: return 'P';
    case ACT_WIFI: return 'W';
    case ACT_SYNC: return 'S';
#ifdef USE_BLE_KEYBOARD_HOST
    case ACT_BTKB: return 'K';
#endif
    case ACT_DRIVE: return 'D'; // "USB Drive" - the D of Drive, not the USB
    case ACT_HELP: return 'H';
    case ACT_UPDATE: return 'U';
    case ACT_ABOUT: return 'A';
    }
    return 0;
}

// Draw a card label with its shortcut letter underlined. Returns nothing; the
// caller has already positioned x for a centered label. The underline is placed
// by measuring the text before the letter, so it works in any font width.
static void drawKeyedLabel(ST7305_4p2_BW_DisplayDriver *d, U8G2_FOR_ST73XX *u8,
                           int x, int baseline, const char *label, char key, uint16_t ink)
{
    u8->setCursor(x, baseline);
    u8->print(label);
    if (!key)
        return;

    int at = -1;
    for (int i = 0; label[i]; i++)
    {
        char c = label[i] >= 'a' && label[i] <= 'z' ? label[i] - 32 : label[i];
        if (c == key) { at = i; break; }
    }
    if (at < 0)
        return; // key not in the label - draw nothing rather than a wrong mark

    char head[24];
    int n = at < (int)sizeof(head) - 1 ? at : (int)sizeof(head) - 1;
    for (int i = 0; i < n; i++)
        head[i] = label[i];
    head[n] = 0;
    char one[2] = {label[at], 0};

    int hx = x + u8->getUTF8Width(head);
    int w = u8->getUTF8Width(one);
    d->drawLine(hx, baseline + 2, hx + w - 2, baseline + 2, ink);
}

// ---- card icons -----------------------------------------------------------
// Each draws inside a 28x28 box at (x,y) in `ink`, so a focused (inverted) card
// gets the same shape in white. Line art only - no glyph, no font dependency.

static void iconPrefs(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // three sliders, knobs at different positions
    for (int i = 0; i < 3; i++)
    {
        int ly = y + 6 + i * 8;
        d->drawLine(x + 2, ly, x + 26, ly, ink);
        d->drawFilledRectangle(x + 4 + i * 8, ly - 3, x + 9 + i * 8, ly + 3, ink);
    }
}

static void iconWifi(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // three widening arcs over a dot
    for (int i = 0; i < 3; i++)
    {
        int s = 4 + i * 5;
        d->drawLine(x + 14 - s, y + 16 - s / 2, x + 14, y + 10 - s, ink);
        d->drawLine(x + 14, y + 10 - s, x + 14 + s, y + 16 - s / 2, ink);
    }
    d->drawFilledRectangle(x + 12, y + 20, x + 16, y + 24, ink);
}

static void iconSync(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // two arrows chasing each other
    d->drawLine(x + 4, y + 9, x + 24, y + 9, ink);
    d->drawFilledTriangle(x + 24, y + 4, x + 24, y + 14, x + 28, y + 9, ink);
    d->drawLine(x + 4, y + 19, x + 24, y + 19, ink);
    d->drawFilledTriangle(x + 4, y + 14, x + 4, y + 24, x, y + 19, ink);
}

static void iconDrive(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // 3.5" floppy, same mark the Storage panel uses
    d->drawRectangle(x + 1, y + 1, x + 27, y + 27, ink);
    d->drawFilledRectangle(x + 8, y + 3, x + 20, y + 11, ink);
    d->drawFilledRectangle(x + 13, y + 4, x + 16, y + 10, ink);
    d->drawRectangle(x + 6, y + 16, x + 22, y + 26, ink);
}

static void iconUpdate(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // arrow landing in a tray
    d->drawLine(x + 14, y + 2, x + 14, y + 14, ink);
    d->drawFilledTriangle(x + 8, y + 12, x + 20, y + 12, x + 14, y + 20, ink);
    d->drawLine(x + 4, y + 24, x + 24, y + 24, ink);
    d->drawLine(x + 4, y + 20, x + 4, y + 24, ink);
    d->drawLine(x + 24, y + 20, x + 24, y + 24, ink);
}

#ifdef USE_BLE_KEYBOARD_HOST
static void iconKeyboard(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    d->drawRectangle(x + 1, y + 6, x + 27, y + 22, ink);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++)
            d->drawFilledRectangle(x + 5 + c * 4, y + 10 + r * 5, x + 7 + c * 4, y + 12 + r * 5, ink);
}
#endif

static void iconHelp(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    // question mark, drawn so it inverts with the card like the rest
    d->drawLine(x + 8, y + 8, x + 10, y + 4, ink);
    d->drawLine(x + 10, y + 4, x + 18, y + 4, ink);
    d->drawLine(x + 18, y + 4, x + 20, y + 8, ink);
    d->drawLine(x + 20, y + 8, x + 14, y + 14, ink);
    d->drawLine(x + 14, y + 14, x + 14, y + 18, ink);
    d->drawFilledRectangle(x + 12, y + 22, x + 16, y + 26, ink);
}

// The device itself, the same deck-with-a-screen mark the About screen draws
// large and the title bar carries small.
static void iconAbout(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    d->drawRectangle(x + 3, y + 2, x + 25, y + 26, ink);
    d->drawRectangle(x + 7, y + 6, x + 21, y + 16, ink);   // screen
    d->drawFilledRectangle(x + 9, y + 20, x + 19, y + 22, ink); // deck
}

static void drawActionIcon(ST7305_4p2_BW_DisplayDriver *d, int act, int x, int y, uint16_t ink)
{
    switch (act)
    {
    case ACT_PREFS: iconPrefs(d, x, y, ink); break;
    case ACT_WIFI: iconWifi(d, x, y, ink); break;
    case ACT_SYNC: iconSync(d, x, y, ink); break;
#ifdef USE_BLE_KEYBOARD_HOST
    case ACT_BTKB: iconKeyboard(d, x, y, ink); break;
#endif
    case ACT_DRIVE: iconDrive(d, x, y, ink); break;
    case ACT_UPDATE: iconUpdate(d, x, y, ink); break;
    case ACT_HELP: iconHelp(d, x, y, ink); break;
    case ACT_ABOUT: iconAbout(d, x, y, ink); break;
    }
}

static void dispatch(int act)
{
    JsonDocument &app = status();
    // sub-screens read this to know which tab to return to on Esc/back
    app["menu"]["return"] = MENU_SETTINGS;
    switch (act)
    {
    case ACT_PREFS: app["menu"]["prefs_from_editor"] = false; app["menu"]["state"] = MENU_PREFS; break;
    case ACT_LANGUAGE: app["menu"]["state"] = MENU_LAYOUT; break;
    case ACT_WIFI: app["menu"]["state"] = MENU_WIFI; break;
    case ACT_SYNC:
        // Reachable when a Drive URL is set OR the provider is git (git syncs
        // via config.sync.git and leaves sync.url empty).
        if (!app["config"]["sync"]["url"].as<String>().isEmpty() ||
            app["config"]["sync"]["provider"].as<String>() == "git")
            app["menu"]["state"] = MENU_SYNC;
        break;
    case ACT_SYNCPROV: app["menu"]["state"] = MENU_SYNCPROV; break;
#ifdef USE_BLE_KEYBOARD_HOST
    case ACT_BTKB: app["menu"]["state"] = MENU_BLUETOOTH; break;
#endif
    case ACT_DRIVE: app["menu"]["state"] = MENU_STORAGE; break;
    case ACT_UPDATE: app["menu"]["state"] = MENU_UPDATE; break;
    case ACT_HELP: app["menu"]["state"] = MENU_HELP; break;
    case ACT_ABOUT: app["menu"]["state"] = MENU_ABOUT; break;
    }
}

void Settings_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_clear();
    g_cursor = 0;
}

void Settings_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    Menu_drawTabs(display, u8, 1);

    int ids[12];
    int n = buildList(ids);

    // Settings as a grid of tiles, three to a row: an icon over the name, with
    // the name's shortcut letter underlined. Each tile is a small window (shadow
    // + frame); the focused one fills black, the same inversion the file list and
    // the hint chips use. The tiles carry their own keys, so this screen needs no
    // footer hint bar.
    const int MX = 10, GAP = 8, COLS = 3;
    const int top = 64, bottom = 292;
    const int ICON = 28, CAP = 12, ICON_GAP = 6; // glyph cap height / icon-to-text gap
    int rows = (n + COLS - 1) / COLS;
    int cardW = (400 - 2 * MX - (COLS - 1) * GAP) / COLS;
    int cardH = (bottom - top - (rows - 1) * GAP) / rows;

    // icon + gap + one text line, centered as one block however tall the card is
    int blockY = (cardH - (ICON + ICON_GAP + CAP)) / 2;
    if (blockY < 4)
        blockY = 4;

    u8->setFont(u8g2_font_profont17_tf);
    for (int i = 0; i < n; i++)
    {
        int cx = MX + (i % COLS) * (cardW + GAP);
        int cy = top + (i / COLS) * (cardH + GAP);
        bool focused = (i == g_cursor);

        RLCD_drawWindow(display, u8, cx, cy, cardW, cardH, nullptr);
        if (focused)
            display->drawFilledRectangle(cx + 1, cy + 1, cx + cardW - 1, cy + cardH - 1, 1);

        uint16_t ink = focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK;
        drawActionIcon(display, ids[i], cx + (cardW - ICON) / 2, cy + blockY, ink);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        const char *label = actionLabel(ids[i]);
        drawKeyedLabel(display, u8, cx + (cardW - u8->getUTF8Width(label)) / 2,
                       cy + blockY + ICON + ICON_GAP + CAP, label, actionKey(ids[i]), ink);

        if (focused)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }
    }
}

void Settings_keyboard(int key)
{
    JsonDocument &app = status();
    int ids[12];
    int n = buildList(ids);

    const int COLS = 3;
    if (key == 20) // Up - previous row
    {
        if (g_cursor >= COLS)
            g_cursor -= COLS;
        Menu_clear();
        return;
    }
    if (key == 21) // Down - next row, clamped to the last (possibly short) row
    {
        if (g_cursor + COLS < n)
            g_cursor += COLS;
        else if (g_cursor / COLS < (n - 1) / COLS)
            g_cursor = n - 1;
        Menu_clear();
        return;
    }
    if (key == 19) // Right - next tile in the row
    {
        if (g_cursor % COLS != COLS - 1 && g_cursor + 1 < n)
            g_cursor++;
        Menu_clear();
        return;
    }
    if (key == 18) // Left - previous tile, or out to FILES from the first column
    {
        if (g_cursor % COLS != 0)
            g_cursor--;
        else
            app["menu"]["state"] = MENU_HOME;
        Menu_clear();
        return;
    }
    if (key == 'B' || key == 'b' || key == '\b') // Back -> Files
    {
        app["menu"]["state"] = MENU_HOME;
        return;
    }
    if (key == 27 || key == MENU) // Esc -> editor
    {
        app["screen"] = WORDPROCESSOR;
        return;
    }

    if (key == '\n' || key == '\r')
    {
        dispatch(ids[g_cursor]);
        return;
    }

    // letter fast-paths (Language / Time zone / Sync provider live in Preferences)
    if (key == 'P' || key == 'p') dispatch(ACT_PREFS);
    else if (key == 'A' || key == 'a') dispatch(ACT_ABOUT);
    else if (key == 'W' || key == 'w') dispatch(ACT_WIFI);
    else if (key == 'S' || key == 's') dispatch(ACT_SYNC);
#ifdef USE_BLE_KEYBOARD_HOST
    else if (key == 'K' || key == 'k') dispatch(ACT_BTKB);
#endif
    else if (key == 'D' || key == 'd') dispatch(ACT_DRIVE);
    else if (key == 'H' || key == 'h') dispatch(ACT_HELP);
    else if (key == 'U' || key == 'u') dispatch(ACT_UPDATE);
}
