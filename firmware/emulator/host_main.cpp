// SDL host for the MicroJournal rev_8 firmware UI. Runs the real display/app/
// editor/keyboard code single-threaded; SDL events are mapped to USB HID usage
// codes and fed through the real keyboard_HID2Ascii() pipeline.
#include <SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "host_display.h"
#include "app/app.h"            // status() — for the headless sync pump
#include "service/Sync/Sync.h"  // SYNC_COMPLETED / SYNC_ERROR

// firmware screen / menu constants (kept in sync with src/display/display.h)
#define HOST_WORDPROCESSOR 0
#define HOST_MENUSCREEN 2
#define HOST_KEYBOARDSCREEN 5
#define HOST_MENU_HOME 0
#define HOST_MENU_STORAGE 12

// host_screen.cpp bridge into firmware app state
int host_screen();
int host_menu_state();
void host_set_screen(int);
void host_set_menu_state(int);
void host_menu_repaint();
bool host_is_fake_screen();

// real firmware entry points
void app_setup();
void app_loop();
void display_setup();
void display_loop();
void keyboard_setup();
void keyboard_loop();
void keyboard_HID2Ascii(uint8_t keycode, uint8_t modifier, bool pressed);

static const int SCALE = 2;

// SDL scancode -> USB HID usage code. 0 = unmapped.
static uint8_t hid_for(SDL_Keycode key)
{
    if (key >= SDLK_a && key <= SDLK_z) return 0x04 + (key - SDLK_a);
    switch (key)
    {
    case SDLK_1: return 0x1e;
    case SDLK_2: return 0x1f;
    case SDLK_3: return 0x20;
    case SDLK_4: return 0x21;
    case SDLK_5: return 0x22;
    case SDLK_6: return 0x23;
    case SDLK_7: return 0x24;
    case SDLK_8: return 0x25;
    case SDLK_9: return 0x26;
    case SDLK_0: return 0x27;
    case SDLK_RETURN: return 0x28;
    case SDLK_ESCAPE: return 0x29; // firmware treats 0x29 as MENU
    case SDLK_BACKSPACE: return 0x2a;
    case SDLK_TAB: return 0x2b;
    case SDLK_SPACE: return 0x2c;
    case SDLK_MINUS: return 0x2d;
    case SDLK_EQUALS: return 0x2e;
    case SDLK_LEFTBRACKET: return 0x2f;
    case SDLK_RIGHTBRACKET: return 0x30;
    case SDLK_BACKSLASH: return 0x31;
    case SDLK_SEMICOLON: return 0x33;
    case SDLK_QUOTE: return 0x34;
    case SDLK_BACKQUOTE: return 0x35;
    case SDLK_COMMA: return 0x36;
    case SDLK_PERIOD: return 0x37;
    case SDLK_SLASH: return 0x38;
    case SDLK_CAPSLOCK: return 0x39;
    case SDLK_DELETE: return 0x4c;
    case SDLK_RIGHT: return 0x4f;
    case SDLK_LEFT: return 0x50;
    case SDLK_DOWN: return 0x51;
    case SDLK_UP: return 0x52;
    case SDLK_HOME: return 0x4a;
    case SDLK_END: return 0x4d;
    case SDLK_PAGEUP: return 0x4b;
    case SDLK_PAGEDOWN: return 0x4e;
    default: return 0;
    }
}

static uint8_t modifier_byte()
{
    SDL_Keymod m = SDL_GetModState();
    uint8_t mod = 0;
    if (m & KMOD_LSHIFT) mod |= (1u << 1);
    if (m & KMOD_RSHIFT) mod |= (1u << 5);
    if (m & KMOD_LALT) mod |= (1u << 2);
    if (m & KMOD_RALT) mod |= (1u << 6);
    if (m & KMOD_LCTRL) mod |= (1u << 0);
    if (m & KMOD_RCTRL) mod |= (1u << 4);
    return mod;
}

// 5x7 hardcoded glyphs for F, A, K, E (1 = lit pixel). Used to stamp a "FAKE"
// badge onto the framebuffer for the four faked screens so it's obvious nothing
// is really transmitting. Drawn for both the live SDL blit and the EMU_DUMP PGM.
static const uint8_t BADGE_GLYPHS[4][7] = {
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
};

static void badge_set(uint8_t *fb, int w, int h, int x, int y)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    fb[y * w + x] = 1;
}

// Stamp "FAKE" into the (writable) framebuffer at the top-right corner, scaled.
static void draw_fake_badge(uint8_t *fb, int w, int h)
{
    const int scale = 3;
    const int gw = 5 * scale, gh = 7 * scale, gap = scale;
    const int total = 4 * gw + 3 * gap;
    int ox = w - total - 10;
    int oy = 6;

    // clear a white box behind the text for legibility
    for (int yy = -2; yy < gh + 2; yy++)
        for (int xx = -2; xx < total + 2; xx++)
        {
            int px = ox + xx, py = oy + yy;
            if (px >= 0 && px < w && py >= 0 && py < h)
                fb[py * w + px] = 0;
        }

    for (int g = 0; g < 4; g++)
    {
        int gx = ox + g * (gw + gap);
        for (int row = 0; row < 7; row++)
            for (int col = 0; col < 5; col++)
                if (BADGE_GLYPHS[g][row] & (1 << (4 - col)))
                    for (int sy = 0; sy < scale; sy++)
                        for (int sx = 0; sx < scale; sx++)
                            badge_set(fb, w, h, gx + col * scale + sx, oy + row * scale + sy);
    }
}

// Convert the 0/1 framebuffer to RGBA. Reflective-LCD greenish tint.
static void blit(SDL_Texture *tex, int w, int h)
{
    void *pixels = nullptr;
    int pitch = 0;
    if (SDL_LockTexture(tex, nullptr, &pixels, &pitch) != 0) return;

    host_fb_lock();
    if (host_is_fake_screen())
        draw_fake_badge(host_fb_mutable(), w, h);
    const uint8_t *fb = host_fb_data();
    for (int y = 0; y < h; y++)
    {
        uint32_t *row = (uint32_t *)((uint8_t *)pixels + y * pitch);
        for (int x = 0; x < w; x++)
        {
            uint8_t on = fb[y * w + x];
            // RGBA8888 (byte order R,G,B,A in memory for SDL_PIXELFORMAT_ABGR8888 differs);
            // use ARGB packing matching SDL_PIXELFORMAT_ARGB8888.
            row[x] = on ? 0xFF1A1A1Au : 0xFFCDE6CDu;
        }
    }
    host_fb_clear_dirty();
    host_fb_unlock();

    SDL_UnlockTexture(tex);
}

// EMU_DUMP path: run setup + a few display loops, write a PGM, count black px.
static int dump_frame(const char *path)
{
    app_setup();
    display_setup();
    keyboard_setup();
    // pump several ticks (display_RLCD_loop is throttled to 100ms; advance time)
    for (int i = 0; i < 30; i++)
    {
        display_loop();
        SDL_Delay(20);
    }
    // Optional: inject a comma-separated list of HID codes (decimal) before the
    // final dump, e.g. EMU_KEYS=41 sends Esc(0x29) to open the MENU. Each code
    // is sent as a press+release. Lets the headless dump exercise other screens.
    // Prefix a code with 'c' to hold Ctrl (e.g. c44 = Ctrl+Space = layout toggle).
    const char *keys = getenv("EMU_KEYS");
    if (keys && *keys)
    {
        char tmp[256];
        strncpy(tmp, keys, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        for (char *tok = strtok(tmp, ","); tok; tok = strtok(nullptr, ","))
        {
            uint8_t mod = 0;
            // optional prefixes: 'c' = hold Ctrl, 's' = hold Shift. Combine
            // freely, e.g. "cs80" = Ctrl+Shift+Left, "s80" = Shift+Left.
            for (;;)
            {
                if (*tok == 'c' || *tok == 'C') { mod |= (1u << 0); tok++; }
                else if (*tok == 's' || *tok == 'S') { mod |= (1u << 1); tok++; }
                else break;
            }
            uint8_t code = (uint8_t)atoi(tok);
            keyboard_HID2Ascii(code, mod, true);
            keyboard_HID2Ascii(code, mod, false);
            // display_RLCD_loop throttles to 100ms and needs a setup+render pair
            // per screen, so give each key several throttled ticks to settle.
            for (int i = 0; i < 8; i++) { display_loop(); SDL_Delay(30); }
        }
    }
    else
    {
        // default: type a char so content changes and the frame has glyphs
        keyboard_HID2Ascii(0x0b, 0, true);  // 'h'
        keyboard_HID2Ascii(0x0b, 0, false);
    }

    // EMU_SYNC: pump app_loop() (which drives sync_loop) until a sync triggered
    // by the injected keys completes. Lets the headless dump run a real Ctrl+U
    // sync end-to-end against the live Drive endpoint.
    if (getenv("EMU_SYNC"))
    {
        for (int i = 0; i < 600; i++)
        {
            app_loop();
            display_loop();
            SDL_Delay(10);
            int st = status()["sync_state"].as<int>();
            if (st == SYNC_COMPLETED || st == SYNC_ERROR)
                break;
        }
        printf("EMU_SYNC: state=%d msg=%s err=%s\n",
               status()["sync_state"].as<int>(),
               status()["sync_message"].as<String>().c_str(),
               status()["sync_error"].as<String>().c_str());
    }

    for (int i = 0; i < 5; i++) { display_loop(); SDL_Delay(20); }

    int w = host_fb_width(), h = host_fb_height();
    host_fb_lock();
    if (host_is_fake_screen())
        draw_fake_badge(host_fb_mutable(), w, h);
    const uint8_t *fb = host_fb_data();
    long black = 0;
    FILE *f = fopen(path, "wb");
    if (f)
    {
        fprintf(f, "P5\n%d %d\n255\n", w, h);
        for (int i = 0; i < w * h; i++)
        {
            uint8_t v = fb[i] ? 0 : 255; // black=0
            fputc(v, f);
            if (fb[i]) black++;
        }
        fclose(f);
    }
    host_fb_unlock();
    printf("EMU_DUMP: wrote %s (%dx%d), black pixels: %ld\n", path, w, h, black);
    return (int)black;
}

int main(int argc, char **argv)
{
    const char *dump = getenv("EMU_DUMP");

    if (SDL_Init(dump ? 0 : SDL_INIT_VIDEO) != 0)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    if (dump)
    {
        int black = dump_frame("frame.pgm");
        SDL_Quit();
        return black > 0 ? 0 : 2;
    }

    int w = host_fb_width(), h = host_fb_height();
    SDL_Window *win = SDL_CreateWindow("MicroJournal rev_8 emulator",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       w * SCALE, h * SCALE, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, w, h);

    printf("MicroJournal rev_8 emulator\n");
    printf("  Type to write. Esc = MENU. Enter/Backspace/Arrows work.\n");
    printf("  In MENU: L=language, W=wifi, U=drive, B=back. Layout via EMU_LAYOUT=AR.\n");
    printf("  Close the window or press Cmd/Ctrl+Q to quit.\n");

    app_setup();
    display_setup();
    keyboard_setup();

    bool running = true;
    while (running)
    {
        SDL_Event e;
        while (SDL_PollEvent(&e))
        {
            if (e.type == SDL_QUIT)
                running = false;
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP)
            {
                // The physical keypad has no OS-style auto-repeat (one press =
                // one event), so ignore SDL key-repeat to match the device -
                // otherwise holding a chord (e.g. paste) floods the buffer.
                if (e.type == SDL_KEYDOWN && e.key.repeat)
                    continue;

                SDL_Keycode k = e.key.keysym.sym;
                if ((e.type == SDL_KEYDOWN) && (e.key.keysym.mod & (KMOD_GUI | KMOD_CTRL)) && k == SDLK_q)
                {
                    running = false;
                    continue;
                }
                // Emulator-side Esc exit for screens whose real handler can't
                // cleanly exit on host (BLE screen has no exit; Drive mode's
                // handler runs a restart loop). Force the transition and DON'T
                // forward the Esc to the firmware. WiFi (B) and Sync (any key
                // after completion) handle their own exit, so leave them alone.
                if (k == SDLK_ESCAPE && e.type == SDL_KEYDOWN)
                {
                    int screen = host_screen();
                    if (screen == HOST_KEYBOARDSCREEN)
                    {
                        host_set_screen(HOST_MENUSCREEN);
                        continue;
                    }
                    if (screen == HOST_MENUSCREEN && host_menu_state() == HOST_MENU_STORAGE)
                    {
                        host_set_menu_state(HOST_MENU_HOME);
                        host_menu_repaint();
                        continue;
                    }
                }

                // EMULATOR-ONLY alias: macOS reserves Ctrl+Left/Right for
                // switching Spaces, so the firmware's word-jump chord never
                // reaches this window. Map Alt(Option)+Left/Right to the same
                // Ctrl+Left/Right the device sends, so word-jump (and Alt+Shift
                // = word-select) is testable on a Mac without disabling the
                // system shortcut. Host-side only; the firmware is unchanged.
                {
                    Uint16 m = e.key.keysym.mod;
                    if ((m & KMOD_ALT) && (k == SDLK_LEFT || k == SDLK_RIGHT))
                    {
                        uint8_t hidArrow = (k == SDLK_RIGHT) ? 0x4f : 0x50;
                        uint8_t mod = modifier_byte();
                        mod &= ~((1u << 2) | (1u << 6)); // drop Alt
                        mod |= (1u << 0);                // add Ctrl
                        keyboard_HID2Ascii(hidArrow, mod, e.type == SDL_KEYDOWN);
                        continue;
                    }
                }

                uint8_t hid = hid_for(k);
                if (hid)
                    keyboard_HID2Ascii(hid, modifier_byte(), e.type == SDL_KEYDOWN);
            }
        }

        keyboard_loop();
        display_loop();
        app_loop();

        if (host_fb_dirty())
        {
            blit(tex, w, h);
            SDL_RenderClear(ren);
            SDL_RenderCopy(ren, tex, nullptr, nullptr);
            SDL_RenderPresent(ren);
        }

        SDL_Delay(16); // ~60fps
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
