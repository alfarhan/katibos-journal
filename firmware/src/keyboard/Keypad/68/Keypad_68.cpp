#include "keypad_68.h"
#include "../Keypad.h"

//
#include "app/app.h"

//
#include "display/display.h"

//
#include "keyboard/Locale/locale.h"

//
#include "keyboard/keyboard.h"

//
#define LAYERS 4 // layers
#define COLS 9   // columns
#define ROWS 8   // rows

// HID keycode (USB HID Usage Tables, Keyboard/Keypad page) for each physical
// key, matching the layout of layers[0]. 0 marks keys that aren't translated
// through the locale tables (control keys, Fn/Shift, MENU, etc).
// prettier-ignore
uint8_t key_hid[ROWS * COLS] = {
    0,    0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2d, 0x2e, 0,    0,
    0x2c, 0x14, 0x1a, 0x08, 0x15, 0x17, 0x1c, 0x18, 0x0c, 0x12, 0x13, 0x2f, 0x30, 0,    0,
    0,    0x04, 0x16, 0x07, 0x09, 0x0a, 0x0b, 0x0d, 0x0e, 0x0f, 0x33, 0x34, 0x31, 0,
    0,    0x35, 0x1d, 0x1b, 0x06, 0x19, 0x05, 0x11, 0x10, 0x36, 0x37, 0x38, 0,    0,    0,
    0,    0,    0,    0x2c, 0,    0,    0,    0,    0,    0,
    0,
};

// layers
//
// Bottom row: rev_8 has no Ctrl in the firmware sense. Upstream's guide.md is
// explicit that the physical Ctrl, Win and Fn keys are ALL reassigned to ALT -
// the layer key, which is 17 here - so every modifier left of the spacebar does
// the same job. The first two used to be 0 (dead), which made the Ctrl keycap do
// nothing at all and left only the inner keys working.
// prettier-ignore
int layers[LAYERS][ROWS * COLS] = {

    {// normal layers
     27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 127,
     ' ', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 22,
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', 23,
     14, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 14, 20, 3,
     17, 17, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// when shift is pressed
     27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', 127,
     ' ', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 22,
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|', 23,
     14, '~', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 14, 20, 3,
     17, 17, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// alt layer
     27, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1000, '-', '=', '\b', 127,
     ' ', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 22,
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', 23,
     14, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 14, 20, 3,
     17, 17, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// alt layer shift
     27, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1000, '-', '=', '\b', 127,
     ' ', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 22,
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|', 23,
     14, '~', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 14, 20, 3,
     17, 17, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

};

// define the symbols on the buttons of the keypads
// prettier-ignore
char keys[ROWS][COLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7, 8},
    {9, 10, 11, 12, 13, 14, 15, 16, 17},
    {18, 19, 20, 21, 22, 23, 24, 25, 26},
    {27, 28, 29, 30, 31, 32, 33, 34, 35},
    {36, 37, 38, 39, 40, 41, 42, 43, 44},
    {45, 46, 47, 48, 49, 50, 51, 52, 53},
    {54, 55, 56, 57, 58, 59, 60, 61, 62},
    {63, 64, 65, 66, 67, 68, 69, 70, 71}};

#ifdef BOARD_PICO
byte rowPins[ROWS] = {0, 1, 2, 3, 4, 5, 6, 7};
byte colPins[COLS] = {13, 14, 15, 16, 17, 18, 19, 20, 21};
#endif

#ifdef BOARD_ESP32_S3
byte rowPins[ROWS] = {8, 18, 17, 16, 15, 7, 6, 5};
byte colPins[COLS] = {1, 2, 42, 41, 40, 39, 45, 48, 47};
#endif

//
Adafruit_Keypad customKeypad = Adafruit_Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

#ifdef BOARD_ESP32_S3
// The matrix is scanned from its own task rather than from the main loop.
//
// Adafruit_Keypad detects an edge only by comparing the pins against the state
// it saw on the PREVIOUS tick, so a key that is pressed and released between two
// ticks produces no event at all - it is not late, it is gone. Rendering a page
// blocks the main loop for ~40ms (measured: median 38, p90 51), which put the
// scan interval well inside the length of a real keystroke, and fast typing
// dropped characters. Scanning on a fixed 5ms task decouples the sample rate
// from how long a frame takes; events pile up in the library's ring buffer and
// the loop drains them when it gets the core back.
//
// Safe without a lock because that ring buffer is single-producer /
// single-consumer: store_char only ever advances _iHead, read_char only _iTail,
// and both are volatile. That holds ONLY while this task is the sole caller of
// tick() - so nothing else may call it, and begin() (which reconfigures the same
// pins) has to stop this task first.
static TaskHandle_t s_scanTask = nullptr;

static void keypad_scan_task(void *)
{
    for (;;)
    {
        customKeypad.tick();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
#endif

// initialize keymap
void keyboard_keypad_68_setup()
{
    // load keyboard.json if exists
    const char *keys[] = {"main", "main-shift", "alt", "alt-shift"};
    keypad_load_config("/keyboard.json", (int *)layers, ROWS * COLS, keys, 4);

    //
    customKeypad.begin();

#ifdef BOARD_ESP32_S3
    // Priority above the Arduino loop task so a long render cannot starve it.
    if (!s_scanTask)
        xTaskCreatePinnedToCore(keypad_scan_task, "keypad_scan", 2560, nullptr,
                                5, &s_scanTask, 0);
#endif

    // reset long press flag
    JsonDocument &app = status();
    app["knobLongPressed"] = false;

    //
    _log("Keypad initialized\n");
}

//
void keyboard_keypad_68_loop()
{
    JsonDocument &app = status();

    // knob long press logic
    static unsigned long knob_press_time = 0;
    static bool knob_pressed = false;

    // check if knob is long pressed - 2 seconds
    if (knob_pressed == true && millis() - knob_press_time > 1000 && app["knobLongPressed"].as<bool>() == false)
    {
        // knob is long pressed
        // mark the flag and send out released event
        app["knobLongPressed"] = true;

        _debug("[keyboard_keypad_68_loop] knob long press detected\n");
        display_keyboard(MENU, KEY_JUST_RELEASED, 69);
    }

    // Drain whatever the scan task has queued. Ungated: after a render there may
    // be several events waiting, and holding them back a further 10ms each is the
    // lag this was meant to remove.
    {
        while (customKeypad.available())
        {
            //
            keypadEvent e = customKeypad.read();
            // Check if knob is long pressed
            // Detect the knob click by the position of the key index
            if (e.bit.KEY == 69 || e.bit.KEY == 0)
            {
                if (e.bit.EVENT == KEY_JUST_PRESSED)
                {
                    knob_press_time = millis();
                    knob_pressed = true;
                }
                else
                {
                    knob_pressed = false;
                    // reset long press flag
                    JsonDocument &app = status();
                    if (app["knobLongPressed"].as<bool>())
                    {
                        app["knobLongPressed"] = false;

                        // do not send out a released event
                        _debug("[keyboard_keypad_68_loop] knob long pressed and ignore actual released event\n");
                        return;
                    }
                }
            }

            //
            // check if the key is pressed
            int character = keyboard_keypad_68_get_key(e);

            // send over the key to the display
            _debug("[keyboard_keypad_68_loop] Key: %d, Event: %d, Row: %d, Col: %d Character: [%d]\n",
                   e.bit.KEY, e.bit.EVENT, e.bit.ROW, e.bit.COL, character);
            display_keyboard(character, e.bit.EVENT == KEY_JUST_PRESSED, e.bit.KEY);
        }
    }
}

//
// Translate the key press into a key code
//
int _layer = 0;
bool _shift_pressed = false;
bool _fn_pressed = false;
//
int keyboard_keypad_68_get_key(keypadEvent e)
{
    // define the layer
    _layer = 0;

    //
    int key = layers[_layer][e.bit.KEY];
    if (key == 17)
    {
        if (e.bit.EVENT == KEY_JUST_PRESSED)
        {
            _fn_pressed = true;
            return 0;
        }
        else
        {
            _fn_pressed = false;
            return 0;
        }
    }
    else if (key == 14)
    {
        if (e.bit.EVENT == KEY_JUST_PRESSED)
        {
            _shift_pressed = true;
            return 0;
        }

        else if (e.bit.EVENT == KEY_JUST_RELEASED)
        {
            _shift_pressed = false;
            return 0;
        }
    }

    // check if the layer key is pressed
    if (_fn_pressed)
        _layer = 2;
    // check if the shift key is pressed
    if (_shift_pressed)
        _layer += 1;

    // value baked into the layer tables (hardcoded to the US layout)
    key = layers[_layer][e.bit.KEY];

    // Fn + Space toggles the typing layout (US <-> Arabic) in place, so the
    // user can switch languages mid-document. Handled before the locale remap
    // so the chord is never turned into a character.
    if (_fn_pressed && key == ' ')
    {
        if (e.bit.EVENT == KEY_JUST_PRESSED)
            keyboard_toggle_layout();
        return 0;
    }

    // Fn + S saves the current document.
    if (_fn_pressed && key == 's')
        return (e.bit.EVENT == KEY_JUST_PRESSED) ? SAVE : 0;

    // Fn + H shows/hides the bottom status bar.
    if (_fn_pressed && key == 'h')
        return (e.bit.EVENT == KEY_JUST_PRESSED) ? STATUSBAR : 0;

    // Fn + / opens the editor shortcut overlay, Fn + G the AI proofread, Fn + U
    // syncs the open file. These exist on the HID path (keyboard_HID2Ascii) and
    // the Help screen lists all three, but the matrix had no chord for them - on
    // a board whose only keyboard IS the matrix that made them unreachable.
    // HELP_KEY is handled once in WP_keyboard with no pressed-guard, so it gets a
    // single edge; the other two are gated on key-down like the clipboard chords.
    if (_fn_pressed && key == '/')
        return (e.bit.EVENT == KEY_JUST_PRESSED) ? HELP_KEY : 0;
    if (_fn_pressed && key == 'g')
        return (e.bit.EVENT == KEY_JUST_PRESSED) ? AI_PROOFREAD : 0;
    if (_fn_pressed && key == 'u')
        return (e.bit.EVENT == KEY_JUST_PRESSED) ? SYNC : 0;

    // Fn + A/C/X/V/Z/Y: select-all / copy / cut / paste / undo / redo.
    if (_fn_pressed && (key == 'a' || key == 'c' || key == 'x' || key == 'v' ||
                        key == 'z' || key == 'y'))
    {
        if (e.bit.EVENT != KEY_JUST_PRESSED)
            return 0;
        switch (key)
        {
        case 'a': return SELECTALL;
        case 'c': return COPY;
        case 'x': return CUT;
        case 'v': return PASTE;
        case 'z': return UNDO;
        case 'y': return REDO;
        }
    }

    // Shift + arrow/Home/End extends the selection (SEL_* codes). The selection
    // grows by character (arrows), line (up/down) or to the line edge (home/end).
    if (_shift_pressed)
    {
        switch (key)
        {
        case 18: return SEL_LEFT;
        case 19: return SEL_RIGHT;
        case 20: return SEL_UP;
        case 21: return SEL_DOWN;
        case 2: return SEL_HOME;
        case 3: return SEL_END;
        }
    }

    // when a non-US layout is configured, re-map character keys through
    // the locale tables instead of returning the hardcoded US character
    JsonDocument &app = status();
    String locale = app["config"]["keyboard_layout"].as<String>();

    // ...but only where the key is meant to be TEXT. Every other screen matches
    // Latin shortcut letters (S = Sync, W = Wi-Fi, X = delete...), so a non-Latin
    // layout would turn each of them into a letter no handler can match and the
    // menu would go dead until the layout was switched back. Forcing US off the
    // text-entry screens is the same rule keyboard_HID2Ascii already applies; the
    // matrix path simply never had it. Rename counts as text entry - that is how
    // an Arabic file title gets typed.
    {
        int screen = app["screen"].as<int>();
        bool textEntry = (screen == WORDPROCESSOR) ||
                         (screen == MENUSCREEN &&
                          app["menu"]["state"].as<int>() == MENU_RENAME);
        if (!textEntry)
            locale = "US";
    }

    if (locale == "INT")
    {
        // International IS the US layout - the layer table already produced the
        // right character, only the accent folding is missing. Feed the RESOLVED
        // character to the filter rather than going through key_hid: the dead
        // keys live on punctuation ( ' " ` ~ ^ ), which has no key_hid entry,
        // and half of them exist only on the LOWER/SHIFT layers a physical-key
        // lookup cannot see. Press events only - a release must not disturb the
        // pending precursor. (Ported from upstream mcu 65ad2c5.)
        if (e.bit.EVENT == KEY_JUST_PRESSED && key >= 32 && key <= 126)
            key = keyboard_precursor_filter(key);
    }
    else if (locale.length() > 0 && locale != "US" && locale != "null")
    {
        uint8_t hid = key_hid[e.bit.KEY];
        if (hid != 0)
        {
            // int, not uint8_t: Arabic and other non-Latin layouts return
            // Unicode code points above 255.
            int ascii = keyboard_keycode_ascii(locale, hid, _shift_pressed, _fn_pressed, e.bit.EVENT == KEY_JUST_PRESSED);
            if (ascii != 0)
                key = ascii;
        }
    }

    //
    //_log("[keyboard_keypad_68_get_key] Layer: %d, Key: %d, HID: %d, ASCII: %d Locale: %s\n", _layer, e.bit.KEY, key_hid[e.bit.KEY], key, locale.c_str());

    // return the corresponding key
    return key;
}

// ---- sleep wake support -----------------------------------------------------
#ifdef BOARD_ESP32_S3

void keypad_prepare_wake()
{
    // Stop the scanner before repurposing its pins, or it fights this wiring and
    // (on the way back) races begin() in keypad_resume_scan().
    if (s_scanTask)
        vTaskSuspend(s_scanTask);

    // Hold every column HIGH so a closed key can source current into its row.
    for (int c = 0; c < COLS; c++)
    {
        pinMode(colPins[c], OUTPUT);
        digitalWrite(colPins[c], HIGH);
    }
    // Rows read LOW while nothing is pressed, HIGH the moment any key closes.
    for (int r = 0; r < ROWS; r++)
        pinMode(rowPins[r], INPUT_PULLDOWN);
}

void keypad_resume_scan()
{
    // Adafruit_Keypad owns the pin directions during a scan; re-running begin()
    // is the supported way to get them back rather than reproducing its setup.
    customKeypad.begin();

    if (s_scanTask)
        vTaskResume(s_scanTask);
}

unsigned long long keypad_wake_mask()
{
    unsigned long long mask = 0;
    for (int r = 0; r < ROWS; r++)
        mask |= (1ULL << rowPins[r]);
    return mask;
}

#else // no matrix wired to this chip - nothing can wake it by key

void keypad_prepare_wake() {}
void keypad_resume_scan() {}
unsigned long long keypad_wake_mask() { return 0; }

#endif
