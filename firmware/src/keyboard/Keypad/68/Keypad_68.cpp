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
// prettier-ignore
int layers[LAYERS][ROWS * COLS] = {

    {// normal layers
     27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', 127,
     ' ', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 22,
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', 23,
     14, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 14, 20, 3,
     0, 0, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// when shift is pressed
     27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', 127,
     ' ', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 22,
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|', 23,
     14, '~', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 14, 20, 3,
     0, 0, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// alt layer
     27, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1000, '-', '=', '\b', 127,
     ' ', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 22,
     0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '\\', 23,
     14, '`', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 14, 20, 3,
     0, 0, 17, ' ', 17, 17, 2, 18, 21, 19,
     MENU},

    {// alt layer shift
     27, 1001, 1002, 1003, 1004, 1005, 1006, 1007, 1008, 1009, 1000, '-', '=', '\b', 127,
     ' ', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 22,
     0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '|', 23,
     14, '~', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 14, 20, 3,
     0, 0, 17, ' ', 17, 17, 2, 18, 21, 19,
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

// initialize keymap
void keyboard_keypad_68_setup()
{
    // load keyboard.json if exists
    const char *keys[] = {"main", "main-shift", "alt", "alt-shift"};
    keypad_load_config("/keyboard.json", (int *)layers, ROWS * COLS, keys, 4);

    //
#ifndef USE_SERIAL_KEYBOARD
    customKeypad.begin();
#endif

    // reset long press flag
    JsonDocument &app = status();
    app["knobLongPressed"] = false;

    //
    _log("Keypad initialized\n");
}

#ifdef USE_SERIAL_KEYBOARD
// Block up to timeout_ms for the next serial byte. Escape-sequence bytes arrive
// back-to-back over USB CDC, so a few ms is enough to gather a full sequence
// without a cross-call state machine. Returns -1 on timeout.
static int serial_wait_byte(uint32_t timeout_ms)
{
    uint32_t start = millis();
    while (!Serial.available())
        if (millis() - start > timeout_ms)
            return -1;
    return Serial.read();
}

// After a leading ESC, decode the rest of a terminal cursor-key sequence into
// the editor's internal navigation codes (same codes the HID path emits). A
// bare Esc keypress (nothing follows) stays 27 = "back to editor". Without this,
// every arrow key arrives as a lone ESC and menu tab navigation is impossible
// over the serial monitor.
static int serial_decode_escape()
{
    int b = serial_wait_byte(5);
    if (b != '[' && b != 'O') // bare Esc, or an unrelated byte
        return 27;

    // Collect CSI numeric params (digits separated by ';') then the final byte.
    // Modified arrows arrive as ESC [ 1 ; <mod> <letter> and Home/End/Del as
    // ESC [ <n> ; <mod> ~, where mod = 1 + shift(1) + alt(2) + ctrl(4). Parsing
    // the whole sequence (not just the first digit) is what stops the trailing
    // "2C"/"6C" from leaking as typed characters.
    int params[3] = {0, 0, 0};
    int np = 0;
    int f = serial_wait_byte(5);
    while (f >= '0' && f <= '9')
    {
        int val = 0;
        while (f >= '0' && f <= '9')
        {
            val = val * 10 + (f - '0');
            f = serial_wait_byte(5);
        }
        if (np < 3)
            params[np++] = val;
        if (f == ';')
        {
            f = serial_wait_byte(5);
            continue;
        }
        break; // f is the final byte
    }

    int mod = (np >= 2) ? params[1] : 1;
    bool shift = (mod - 1) & 1;
    bool ctrl = (mod - 1) & 4;

    switch (f)
    {
    case 'A': return shift ? SEL_UP : (ctrl ? PARA_UP : 20);                        // Up
    case 'B': return shift ? SEL_DOWN : (ctrl ? PARA_DOWN : 21);                    // Down
    case 'C': return shift ? (ctrl ? SEL_WORD_RIGHT : SEL_RIGHT) : (ctrl ? WORD_RIGHT : 19); // Right
    case 'D': return shift ? (ctrl ? SEL_WORD_LEFT : SEL_LEFT) : (ctrl ? WORD_LEFT : 18);    // Left
    case 'H': return shift ? SEL_HOME : (ctrl ? DOC_TOP : 2);                       // Home
    case 'F': return shift ? SEL_END : (ctrl ? DOC_BOTTOM : 3);                     // End
    case '~':
        switch (params[0])
        {
        case 1:
        case 7: return shift ? SEL_HOME : (ctrl ? DOC_TOP : 2);    // Home
        case 3: return 127;                                        // Delete (forward)
        case 4:
        case 8: return shift ? SEL_END : (ctrl ? DOC_BOTTOM : 3);  // End
        case 5: return 22;                                         // Page Up
        case 6: return 23;                                         // Page Down
        }
        break;
    }
    return 0; // unknown sequence: swallow
}

// Decode a multi-byte UTF-8 character (lead byte already read) into its Unicode
// code point. The editor and locale tables work in code points (e.g. 0x0627 for
// ا), so typing Arabic — or any non-Latin text — into the serial monitor arrives
// as UTF-8 and must be folded back to one code point; otherwise each raw byte is
// inserted as a separate Latin-1 glyph ("rubbish"). Returns 0 on a malformed
// sequence.
static int serial_decode_utf8(int lead)
{
    int need, cp;
    if ((lead & 0xE0) == 0xC0) { need = 1; cp = lead & 0x1F; }
    else if ((lead & 0xF0) == 0xE0) { need = 2; cp = lead & 0x0F; }
    else if ((lead & 0xF8) == 0xF0) { need = 3; cp = lead & 0x07; }
    else return 0; // stray continuation byte / invalid lead
    for (int i = 0; i < need; i++)
    {
        int b = serial_wait_byte(5);
        if (b < 0x80 || b > 0xBF)
            return 0; // truncated: not a continuation byte
        cp = (cp << 6) | (b & 0x3F);
    }
    return cp;
}
#endif

//
void keyboard_keypad_68_loop()
{
#ifndef USE_SERIAL_KEYBOARD
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

    //
    static unsigned int last = 0;
    if (millis() - last > 10)
    {
        //
        last = millis();

        // put your main code here, to run repeatedly:
        customKeypad.tick();
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
#endif // !USE_SERIAL_KEYBOARD

#ifdef USE_SERIAL_KEYBOARD
    // receive SERIAL input and redirect to the display
    if (Serial.available())
    {
        int c = Serial.read();
        if (c == 13)
            return; // ignore \r key

        // Terminal arrow/nav keys arrive as ANSI escape sequences; decode them
        // to the internal codes so menu tabs (Files/Settings) and editor
        // caret movement work over serial. A decoded nav key is dispatched here
        // and returns, so it bypasses the control-byte shortcut mapping below
        // (which shares the same small code values). A bare Esc (k == 27) falls
        // through and behaves as "back to editor".
        if (c == 27)
        {
            int k = serial_decode_escape();
            if (k == 0)
                return; // unknown/incomplete sequence: swallow
            if (k != 27)
            {
                display_keyboard(k, true);
                display_keyboard(k, false);
                return;
            }
        }

        // The serial monitor carries no modifier bits, so a Ctrl chord arrives as
        // its raw C0 byte. Map the editor shortcuts the HID path exposes: Ctrl+S
        // = save, Ctrl+U = sync the open file. (Ctrl+Shift+U / sync-all can't be
        // distinguished from Ctrl+U over serial — use the Sync tab for that.)
        if (c == 19) { display_keyboard(SAVE, false); return; } // Ctrl+S
        if (c == 21) { display_keyboard(SYNC, false); return; } // Ctrl+U
        // Ctrl+H is byte 0x08 = Backspace over serial and can't be told apart,
        // so the status-bar toggle (Ctrl+H on HID) gets Ctrl+G here instead.
        if (c == 7)  { display_keyboard(STATUSBAR, false); return; } // Ctrl+G

        // Editor command chords, mirroring the HID Ctrl+key set. Gated to the
        // relevant screen so they don't fire in menus. Ctrl+C (0x03) may be
        // swallowed by the terminal; the rest are unambiguous serial bytes.
        // Ctrl+Shift+Z (redo on HID) can't be distinguished from Ctrl+Z here —
        // use Ctrl+Y for redo.
        {
            int sc = status()["screen"].as<int>();
            bool editor = (sc == WORDPROCESSOR);
            bool textEntry = editor ||
                             (sc == MENUSCREEN &&
                              status()["menu"]["state"].as<int>() == MENU_RENAME);

            // DATE_INSERT and HELP_KEY are handled once in WP_keyboard with no
            // pressed-guard, so emit a single edge (both edges would double-fire).
            if (c == 4 && textEntry) { display_keyboard(DATE_INSERT, false); return; } // Ctrl+D
            if (c == 31 && editor)   { display_keyboard(HELP_KEY, false); return; }     // Ctrl+/

            // Clipboard/select-all/undo/redo are gated to the key-down edge in
            // Editor::keyboardImpl, so emit press+release (release is a no-op).
            if (editor)
            {
                int cmd = 0;
                switch (c)
                {
                case 1:  cmd = SELECTALL; break; // Ctrl+A
                case 3:  cmd = COPY;      break; // Ctrl+C
                case 22: cmd = PASTE;     break; // Ctrl+V
                case 24: cmd = CUT;       break; // Ctrl+X
                case 25: cmd = REDO;      break; // Ctrl+Y
                case 26: cmd = UNDO;      break; // Ctrl+Z
                }
                if (cmd)
                {
                    display_keyboard(cmd, true);
                    display_keyboard(cmd, false);
                    return;
                }
            }
        }

        // Non-ASCII bytes are the start of a UTF-8 sequence (Arabic, accents,
        // emoji…). Fold the whole sequence into one Unicode code point so the
        // editor stores/renders it the same as the HID keyboard path would.
        if (c >= 0x80)
        {
            c = serial_decode_utf8(c);
            if (c <= 0)
                return; // malformed / stray byte: swallow

            // Off text-entry screens, fold a localized letter back to its US
            // base so menu shortcuts (S=Sync, W=WiFi…) match in any typing
            // layout. The HID path gets this free by using the US table in
            // menus; serial carries no keycode, so we reverse the code point.
            {
                JsonDocument &app = status();
                int screen = app["screen"].as<int>();
                bool textEntry = (screen == WORDPROCESSOR) ||
                                 (screen == MENUSCREEN &&
                                  app["menu"]["state"].as<int>() == MENU_RENAME);
                if (!textEntry)
                    c = keyboard_us_equivalent(
                        app["config"]["keyboard_layout"].as<String>(), c);
            }
        }

        _debug("Serial keyboard input %c %d\n", c, c);
        display_keyboard(c, true);  // Key press
        display_keyboard(c, false); // Key release (optional, for GUI consistency)
    }
#endif
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
    if (locale.length() > 0 && locale != "US" && locale != "null")
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
