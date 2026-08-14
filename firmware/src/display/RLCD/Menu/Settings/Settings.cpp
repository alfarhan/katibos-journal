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
// Each is drawn on a 28-unit grid and scaled to whatever box it is given, so the
// same mark serves the roomy grid and the narrow column beside the file list.
// Line art only - no glyph, no font dependency - and everything is drawn in
// `ink`, so a focused (inverted) card gets the same shape in white.
#define S(v) ((v) * sz / 28)

static void iconPrefs(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    for (int i = 0; i < 3; i++)
    {
        int ly = y + S(6 + i * 8);
        d->drawLine(x + S(2), ly, x + S(26), ly, ink);
        d->drawFilledRectangle(x + S(4 + i * 8), ly - S(3), x + S(9 + i * 8), ly + S(3), ink);
    }
}

static void iconWifi(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    for (int i = 0; i < 3; i++)
    {
        int a = 4 + i * 5;
        d->drawLine(x + S(14 - a), y + S(16 - a / 2), x + S(14), y + S(10 - a), ink);
        d->drawLine(x + S(14), y + S(10 - a), x + S(14 + a), y + S(16 - a / 2), ink);
    }
    d->drawFilledRectangle(x + S(12), y + S(20), x + S(16), y + S(24), ink);
}

static void iconSync(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawLine(x + S(4), y + S(9), x + S(24), y + S(9), ink);
    d->drawFilledTriangle(x + S(24), y + S(4), x + S(24), y + S(14), x + S(28), y + S(9), ink);
    d->drawLine(x + S(4), y + S(19), x + S(24), y + S(19), ink);
    d->drawFilledTriangle(x + S(4), y + S(14), x + S(4), y + S(24), x, y + S(19), ink);
}

static void iconDrive(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawRectangle(x + S(1), y + S(1), x + S(27), y + S(27), ink);
    d->drawFilledRectangle(x + S(8), y + S(3), x + S(20), y + S(11), ink);
    d->drawFilledRectangle(x + S(13), y + S(4), x + S(16), y + S(10), ink);
    d->drawRectangle(x + S(6), y + S(16), x + S(22), y + S(26), ink);
}

static void iconUpdate(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawLine(x + S(14), y + S(2), x + S(14), y + S(14), ink);
    d->drawFilledTriangle(x + S(8), y + S(12), x + S(20), y + S(12), x + S(14), y + S(20), ink);
    d->drawLine(x + S(4), y + S(24), x + S(24), y + S(24), ink);
    d->drawLine(x + S(4), y + S(20), x + S(4), y + S(24), ink);
    d->drawLine(x + S(24), y + S(20), x + S(24), y + S(24), ink);
}

#ifdef USE_BLE_KEYBOARD_HOST
static void iconKeyboard(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawRectangle(x + S(1), y + S(6), x + S(27), y + S(22), ink);
    for (int r = 0; r < 2; r++)
        for (int c = 0; c < 5; c++)
            d->drawFilledRectangle(x + S(5 + c * 4), y + S(10 + r * 5),
                                   x + S(7 + c * 4), y + S(12 + r * 5), ink);
}
#endif

static void iconHelp(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawLine(x + S(8), y + S(8), x + S(10), y + S(4), ink);
    d->drawLine(x + S(10), y + S(4), x + S(18), y + S(4), ink);
    d->drawLine(x + S(18), y + S(4), x + S(20), y + S(8), ink);
    d->drawLine(x + S(20), y + S(8), x + S(14), y + S(14), ink);
    d->drawLine(x + S(14), y + S(14), x + S(14), y + S(18), ink);
    d->drawFilledRectangle(x + S(12), y + S(22), x + S(16), y + S(26), ink);
}

static void iconAbout(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawRectangle(x + S(3), y + S(2), x + S(25), y + S(26), ink);
    d->drawRectangle(x + S(7), y + S(6), x + S(21), y + S(16), ink);
    d->drawFilledRectangle(x + S(9), y + S(20), x + S(19), y + S(22), ink);
}

static void iconRestart(ST7305_4p2_BW_DisplayDriver *d, int x, int y, int sz, uint16_t ink)
{
    d->drawCircle(x + S(14), y + S(15), S(9), ink);
    if (sz >= 26)
        d->drawCircle(x + S(14), y + S(15), S(8), ink); // 2px stroke only when big
    d->drawFilledTriangle(x + S(12), y + S(2), x + S(12), y + S(10), x + S(19), y + S(6), ink);
}

static void drawActionIcon(ST7305_4p2_BW_DisplayDriver *d, int act, int x, int y, int sz, uint16_t ink)
{
    switch (act)
    {
    case ACT_PREFS: iconPrefs(d, x, y, sz, ink); break;
    case ACT_WIFI: iconWifi(d, x, y, sz, ink); break;
    case ACT_SYNC: iconSync(d, x, y, sz, ink); break;
#ifdef USE_BLE_KEYBOARD_HOST
    case ACT_BTKB: iconKeyboard(d, x, y, sz, ink); break;
#endif
    case ACT_DRIVE: iconDrive(d, x, y, sz, ink); break;
    case ACT_UPDATE: iconUpdate(d, x, y, sz, ink); break;
    case ACT_HELP: iconHelp(d, x, y, sz, ink); break;
    case ACT_ABOUT: iconAbout(d, x, y, sz, ink); break;
    case ACT_RESTART: iconRestart(d, x, y, sz, ink); break;
    }
}
#undef S

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
    // The mark is sized to the card rather than the card to the mark, so a narrow
    // column keeps its frames instead of having the icon run through them.
    const int ICON = (h >= 44) ? 28 : (h >= 30 ? 20 : 16);
    RLCD_drawWindow(display, u8, x, y, w, h, nullptr);
    if (focused)
        display->drawFilledRectangle(x + 1, y + 1, x + w - 1, y + h - 1, 1);

    uint16_t ink = focused ? ST7305_COLOR_WHITE : ST7305_COLOR_BLACK;
    if (focused)
    {
        u8->setForegroundColor(ST7305_COLOR_WHITE);
        u8->setBackgroundColor(ST7305_COLOR_BLACK);
    }
    u8->setFont(u8g2_font_profont17_tf);
    const char *label = actionLabel(id);

    const int CAP = 12, GAP = 8;
    if (h < 28 + CAP + 12)
    {
        // Short and wide: mark on the left, name beside it, both centred on the
        // card's middle line. This is what lets a card be half as tall - nothing
        // is stacked, so the height only has to clear the icon.
        // Tight margins: "Preferences" is 98px and the column is 146, so the mark
        // and the name have about 10px between them and the edges to share.
        int iy = y + (h - ICON) / 2;
        drawActionIcon(display, id, x + 8, iy, ICON, ink);
        drawKeyedLabel(display, u8, x + 8 + ICON + GAP, y + (h + CAP) / 2, label,
                       actionKey(id), ink);
    }
    else
    {
        // Tall enough to stack: mark over the name, centred.
        int blockY = (h - (ICON + 6 + CAP)) / 2;
        if (blockY < 2)
            blockY = 2;
        drawActionIcon(display, id, x + (w - ICON) / 2, y + blockY, ICON, ink);
        drawKeyedLabel(display, u8, x + (w - u8->getUTF8Width(label)) / 2,
                       y + blockY + ICON + 6 + CAP, label, actionKey(id), ink);
    }

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
