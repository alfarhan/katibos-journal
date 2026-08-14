#include "Settings.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"
#include "display/RLCD/Menu/FileList/Pagination.h"
#include "service/Editor/Editor.h"
#include "service/Updater/Ota.h" // ota_reboot()

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
    ACT_ABOUT,
    ACT_RESTART
};

// T ("resTart") asks to restart, behind a confirm: a boot costs a few seconds
// and, on a BLE build, re-pairing the keyboard. Not R - that is rename on the
// Files tab, and one letter meaning two things per tab is the collision this key
// map has been bitten by before.
static bool restart_confirm = false;

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
    ids[n++] = ACT_RESTART;
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
    case ACT_RESTART: return "Restart";
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
    case ACT_RESTART: return 'T'; // "resTart" - R is rename on the Files tab
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

// A circle broken at the top right with an arrowhead on the loose end - the
// restart mark. drawCircle then a cleared notch, because there is no arc.
static void iconRestart(ST7305_4p2_BW_DisplayDriver *d, int x, int y, uint16_t ink)
{
    d->drawCircle(x + 14, y + 15, 9, ink);
    d->drawCircle(x + 14, y + 15, 8, ink); // 2px stroke, so it reads at this size
    // knock the gap out of the top-right quadrant
    d->drawFilledRectangle(x + 14, y + 3, x + 26, y + 9, ink == ST7305_COLOR_WHITE ? ST7305_COLOR_BLACK : ST7305_COLOR_WHITE);
    d->drawFilledTriangle(x + 12, y + 2, x + 12, y + 10, x + 19, y + 6, ink);
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
    case ACT_RESTART: iconRestart(d, x, y, ink); break;
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
    case ACT_RESTART: restart_confirm = true; Menu_clear(); break;
    }
}

// The FILES/SETTINGS tabs are one screen now: Home draws these cards and routes
// their keys, so this file is the card MODEL only. Its old setup/render/keyboard
// entry points are gone rather than left to rot.

// ---- shared card model (see Settings.h) -------------------------------------
// Thin wrappers so the combined Home screen can draw and drive the same cards
// without duplicating the list, the icons or the dispatch.
int Settings_cards(int *ids, int max)
{
    int tmp[16];
    int n = buildList(tmp);
    if (n > max)
        n = max;
    for (int i = 0; i < n; i++)
        ids[i] = tmp[i];
    return n;
}

const char *Settings_cardLabel(int id) { return actionLabel(id); }
char Settings_cardKey(int id) { return actionKey(id); }
void Settings_openCard(int id) { dispatch(id); }

// One card, exactly as the Settings grid draws it: framed window, inverted when
// focused, icon centred over the name with the shortcut letter underlined.
void Settings_drawCard(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8,
                       int id, int x, int y, int w, int h, bool focused)
{
    // The combined Home screen gives a card less height than the full grid does,
    // so the mark shrinks rather than pushing the name off the bottom edge.
    const int ICON = (h >= 60) ? 28 : 20;
    const int CAP = 12, ICON_GAP = 6;
    int blockY = (h - (ICON + ICON_GAP + CAP)) / 2;
    if (blockY < 2)
        blockY = 2;

    RLCD_drawWindow(display, u8, x, y, w, h, nullptr);
    if (focused)
        display->drawFilledRectangle(x + 1, y + 1, x + w - 1, y + h - 1, 1);

    uint16_t ink = focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK;
    drawActionIcon(display, id, x + (w - ICON) / 2, y + blockY, ink);

    if (focused)
    {
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
    }
    u8->setFont(u8g2_font_profont17_tf);
    const char *label = actionLabel(id);
    drawKeyedLabel(display, u8, x + (w - u8->getUTF8Width(label)) / 2,
                   y + blockY + ICON + ICON_GAP + CAP, label, actionKey(id), ink);
    if (focused)
    {
        u8->setForegroundColor(ST7305_COLOR_BLACK);
        u8->setBackgroundColor(ST7305_COLOR_WHITE);
    }
}

bool Settings_letter(int key)
{
    int up = (key >= 'a' && key <= 'z') ? key - 32 : key;
    int ids[16];
    int n = Settings_cards(ids, 16);
    for (int i = 0; i < n; i++)
        if (actionKey(ids[i]) && actionKey(ids[i]) == up)
        {
            dispatch(ids[i]);
            return true;
        }
    return false;
}

bool Settings_confirmActive() { return restart_confirm; }

void Settings_drawConfirm(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    const int bx = 40, by = 96, bw = 320, bh = 104;
    RLCD_drawWindow(display, u8, bx, by, bw, bh, "RESTART");
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(bx + 16, by + 50);
    u8->print("Restart the device?");
    u8->setCursor(bx + 16, by + 74);
    u8->print("Any open file is saved first.");
    static const RLCD_Hint HINTS[] = {{"Y", "RESTART"}, {"ANY", "CANCEL"}};
    RLCD_drawHintBar(display, u8, bx + (bw - RLCD_hintBarWidth(u8, RLCD_HINTS(HINTS))) / 2,
                     by + bh + 30, RLCD_HINTS(HINTS));
}

void Settings_confirmKey(int key)
{
    restart_confirm = false;
    Menu_clear();
    if (key == 'Y' || key == 'y')
    {
        Editor::getInstance().saveFile();
        _log("[settings] restart confirmed\n");
        ota_reboot();
    }
}
