#include "keyboard.h"
#include "app/app.h"
#include "display/display.h"

// marks the menu dirty so it repaints (defined in the RLCD Menu module)
void Menu_clear();
#include "display/RLCD/Menu/Home/Home.h" // Home_focusCards - Ctrl+. targets the cards

//
#include "keyboard/Locale/locale.h"

#ifdef KEYPAD_68
#include "keyboard/Keypad/68/keypad_68.h"
#endif

#ifdef KEYPAD_48
#include "keyboard/Keypad/48/keypad_48.h"
#endif

#ifdef KNOB
#include "keyboard/Knob/Knob.h"
#endif

#ifdef REV7
#include "keyboard/USBHost/USBHost.h"
#endif

#ifdef REV6
#include "keyboard/Keypad/48/keypad_48.h"
#endif

#ifdef CARDPUTER
#include "keyboard/CardPuter/keypad_CardPuter.h"
#include "keyboard/USBHost/USBHost.h"
#include "keyboard/BLE/ble.h"
#endif

#ifdef REV5
#include "keyboard/USBHost/USBHost.h"
#include "keyboard/Button/button.h"
#include "keyboard/BLE/ble.h"
#endif

#ifdef BOARD_PICO
#include "KeyboardTinyUSB.h"
#endif

//
void keyboard_setup()
{
#if defined(BOARD_PICO)
  // Register the boot-protocol HID interface at boot, not lazily when the
  // on-screen keyboard first shows, so it's present for BIOS/UEFI POST.
  Keyboard.begin();

  // The arduino-pico core connects to the USB bus and the host may
  // enumerate before setup() gets around to registering the HID
  // keyboard/MSC interfaces above (display/filesystem init runs first
  // and isn't instant) -- a host that already enumerated won't pick up
  // interfaces added afterwards without a fresh attach cycle.
  if (TinyUSBDevice.mounted())
  {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
#endif

#ifdef REV7
  // setup USB Host
  USBHost_setup();
#endif

#ifdef KEYPAD_68
  keyboard_keypad_68_setup();
#endif

#if defined(KEYPAD_48) && defined(BOARD_PICO)
  keyboard_keypad_48_setup();
#endif

#ifdef KNOB
  knob_setup();
#endif

#ifdef REV5
  // setup USB Host
  USBHost_setup();

  // setup BLE Keyboard
  ble_setup("Micro Journal 5");

  // Front Button Setup
  button_setup();
#endif

#ifdef REV6
  keyboard_keypad_48_setup();
#endif

#ifdef CARDPUTER
  keypad_cardputer_setup();

  // setup USB Host
  USBHost_setup();

  // setup BLE Keyboard
  ble_setup("MJ CARDPUTER");
#endif
}

//
void keyboard_loop()
{
#ifdef REV7
  USBHost_loop();
#endif

#ifdef REV6
  keyboard_keypad_48_loop();
#endif

#ifdef CARDPUTER
  keypad_cardputer_loop();

  // setup USB Host
  USBHost_loop();

  // setup BLE Keyboard
  ble_loop();
#endif

#ifdef REV5
  // setup USB Host
  USBHost_loop();

  // setup BLE Keyboard
  ble_loop();

  // Front Button Setup
  button_loop();
#endif

#ifdef KEYPAD_68
  keyboard_keypad_68_loop();
#endif

#if defined(KEYPAD_48) && defined(BOARD_PICO)
  keyboard_keypad_48_loop();
#endif

#ifdef KNOB
  knob_loop();
#endif
}

// capslock
bool _capslock = false;
bool keyboard_capslock()
{
  return _capslock;
}
void keyboard_capslock_toggle()
{
  _capslock = !_capslock;
}

// Flip between Arabic and the last Latin layout, persisting the choice. Lets
// the user switch languages mid-document without going through the menu.
void keyboard_toggle_layout()
{
  JsonDocument &app = status();
  static String lastLatin = "US";
  static String lastArabic = "AR"; // remembers Mac vs Windows Arabic
  String cur = app["config"]["keyboard_layout"].as<String>();

  if (keyboard_locale_is_arabic(cur))
  {
    lastArabic = cur;
    app["config"]["keyboard_layout"] = lastLatin.length() ? lastLatin : String("US");
  }
  else
  {
    if (cur.length() > 0)
      lastLatin = cur;
    app["config"]["keyboard_layout"] = lastArabic.length() ? lastArabic : String("AR");
  }

  config_save();
  _log("Layout toggled to %s\n", app["config"]["keyboard_layout"].as<String>().c_str());
}

//
void keyboard_HID2Ascii(uint8_t keycode, uint8_t modifier, bool pressed)
{
  //////////////////////////////////////////
  // CAPSLOCK is pressed
  // 0x39 - HID_KEY_CAPS_LOCK
  if (keycode == 0x39 && pressed)
  {
    // Mark caps lock status
    keyboard_capslock_toggle();
    return;
  }

  //////////////////////////////////////////
  // MENU
  // ESC key is MENU button
  if (keycode == 0x29)
  {
    display_keyboard(MENU, pressed, 69);
    return;
  }

  //////////////////////////////////////////
  // Cmd + . (period) = MENU/back. Kept for external HID keyboards with no Esc
  // key: Cmd+. is the Mac-standard "cancel", and both its keycode (0x37) and the
  // GUI modifier (bit 3 left / bit 7 right) ride the standard boot report.
  if (keycode == 0x37 && (modifier & 0x88))
  {
    display_keyboard(MENU, pressed, 69);
    return;
  }

  //////////////////////////////////////////
  // SHIFT/ALT/CTRL state (computed early so the nav switch can branch on Shift
  // for selection-extend).
  // TU_BIT(1) - (1UL << (1))
  // KEYBOARD_MODIFIER_LEFTCTRL   = TU_BIT(0), ///< Left Control
  // KEYBOARD_MODIFIER_LEFTSHIFT  = TU_BIT(1), ///< Left Shift
  // KEYBOARD_MODIFIER_LEFTALT    = TU_BIT(2), ///< Left Alt
  // KEYBOARD_MODIFIER_LEFTGUI    = TU_BIT(3), ///< Left Window
  // KEYBOARD_MODIFIER_RIGHTCTRL  = TU_BIT(4), ///< Right Control
  // KEYBOARD_MODIFIER_RIGHTSHIFT = TU_BIT(5), ///< Right Shift
  // KEYBOARD_MODIFIER_RIGHTALT   = TU_BIT(6), ///< Right Alt
  // KEYBOARD_MODIFIER_RIGHTGUI   = TU_BIT(7)  ///< Right Window
  bool shift = (modifier & (1UL << (1))) || (modifier & (1UL << (5)));

  // Check ALT key pressed
  bool alt = (modifier & (1UL << (2))) || (modifier & (1UL << (6)));

  // Check CTRL key pressed
  bool ctrl = (modifier & (1UL << (0))) || (modifier & (1UL << (4)));

  //////////////////////////////////////////
  // Navigation / editing keys are layout-independent (not text), so map their
  // HID usage codes straight to the editor's command codes here, bypassing the
  // locale tables - a non-Latin layout (e.g. Arabic) doesn't define them, which
  // otherwise leaves the arrows/Home/End/PgUp/PgDn/Del dead.
  // With Shift held, the arrows/Home/End extend the selection instead (Ctrl+Shift
  // on Left/Right extends by word). Ctrl alone on Left/Right jumps a word without
  // selecting. Word boundaries are space/newline, so this is layout-independent
  // (works the same for Arabic and English).
  switch (keycode)
  {
  case 0x4f: display_keyboard(shift ? (ctrl ? SEL_WORD_RIGHT : SEL_RIGHT) : (ctrl ? WORD_RIGHT : 19), pressed, keycode); return; // Right
  case 0x50: display_keyboard(shift ? (ctrl ? SEL_WORD_LEFT : SEL_LEFT) : (ctrl ? WORD_LEFT : 18), pressed, keycode); return;   // Left
  case 0x51: display_keyboard(shift ? SEL_DOWN : (ctrl ? PARA_DOWN : 21), pressed, keycode); return;       // Down / Ctrl+Down = next paragraph
  case 0x52: display_keyboard(shift ? SEL_UP : (ctrl ? PARA_UP : 20), pressed, keycode); return;           // Up / Ctrl+Up = prev paragraph
  case 0x4a: display_keyboard(shift ? SEL_HOME : (ctrl ? DOC_TOP : 2), pressed, keycode); return;          // Home / Ctrl+Home
  case 0x4d: display_keyboard(shift ? SEL_END : (ctrl ? DOC_BOTTOM : 3), pressed, keycode); return;        // End / Ctrl+End
  case 0x4b: display_keyboard(22, pressed, keycode); return;  // Page Up
  case 0x4e: display_keyboard(23, pressed, keycode); return;  // Page Down
  case 0x4c: display_keyboard(127, pressed, keycode); return; // Delete (forward)
  // Tab is a space IN THE EDITOR, in every layout (the editor drops a raw '\t',
  // and the Arabic table emitted one, so Tab did nothing in Arabic). Outside the
  // editor it stays a Tab, because the home screen uses it to cross between the
  // file list and the settings cards.
  case 0x2b:
    display_keyboard(status()["screen"].as<int>() == WORDPROCESSOR ? ' ' : '\t',
                     pressed, keycode);
    return;
  default: break;
  }

  // Ctrl + Space toggles the typing layout (US <-> Arabic) in place. Mirrors
  // Fn + Space on the physical keypad. Intercepted here, before locale
  // translation, so the chord is never mapped to a letter. Allowed in any
  // text-entry context: the editor and the Rename field.
  {
    int sc = status()["screen"].as<int>();
    bool textEntry = (sc == WORDPROCESSOR) ||
                     (sc == MENUSCREEN && status()["menu"]["state"].as<int>() == MENU_RENAME);
    if (keycode == 0x2c && ctrl && textEntry)
    {
      if (pressed)
        keyboard_toggle_layout();
      return;
    }
  }

  // Ctrl + S saves the current document (mirrors Fn + S on the keypad). Emitted
  // once, on press, as a SAVE action code the word processor handles.
  if (keycode == 0x16 && ctrl && status()["screen"].as<int>() == WORDPROCESSOR)
  {
    if (pressed)
      display_keyboard(SAVE, false, keycode);
    return;
  }

  // Ctrl + H shows/hides the bottom status bar (mirrors Fn + H on the keypad).
  if (keycode == 0x0b && ctrl && status()["screen"].as<int>() == WORDPROCESSOR)
  {
    if (pressed)
      display_keyboard(STATUSBAR, false, keycode);
    return;
  }

  // Ctrl + U  = sync the OPEN file (editor only).
  // Ctrl+Shift+U = sync ALL files (editor or any menu screen, e.g. FILES/Sync).
  // Detected by physical keycode so it works under any keyboard layout.
  if (keycode == 0x18 && ctrl)
  {
    int sc = status()["screen"].as<int>();
    if (shift)
    {
      if (sc == WORDPROCESSOR || sc == MENUSCREEN)
      {
        if (pressed)
          display_keyboard(SYNC_ALL, false, keycode);
        return;
      }
    }
    else if (sc == WORDPROCESSOR)
    {
      if (pressed)
        display_keyboard(SYNC, false, keycode);
      return;
    }
  }

  // Ctrl + G sends the OPEN file to the AI speller (editor only). Keycode, not
  // ASCII, so it works under any keyboard layout - same as Ctrl+U above.
  if (keycode == 0x0a && ctrl && status()["screen"].as<int>() == WORDPROCESSOR)
  {
    if (pressed)
      display_keyboard(AI_PROOFREAD, false, keycode);
    return;
  }

  // Ctrl + / (or Ctrl+?) opens the editor shortcut overlay; any key closes it.
  if (keycode == 0x38 && ctrl && status()["screen"].as<int>() == WORDPROCESSOR)
  {
    if (pressed)
      display_keyboard(HELP_KEY, false, keycode);
    return;
  }

  // Ctrl + , opens Preferences from anywhere (editor or menu). Navigate directly:
  // from the editor we set a one-shot "goto" that Menu_setup honors (it otherwise
  // resets to FILES on menu entry); already in the menu we just switch state.
  if (keycode == 0x36 && ctrl)
  {
    if (pressed)
    {
      JsonDocument &app = status();
      if (app["screen"].as<int>() == MENUSCREEN)
      {
        app["menu"]["prefs_from_editor"] = false;
        app["menu"]["state"] = MENU_PREFS;
        Menu_clear(); // mark dirty so the menu repaints the new screen
      }
      else
      {
        app["menu"]["prefs_from_editor"] = true;
        app["menu"]["goto"] = MENU_PREFS;
        app["screen"] = MENUSCREEN;
      }
    }
    return;
  }

  // Ctrl + . puts the cursor on the settings CARDS, the same way Ctrl + , opens
  // Preferences - the two neighbouring keys reach the two setting surfaces. There
  // is no separate SETTINGS screen any more, so this focuses the lower half of the
  // combined home rather than switching screens.
  if (keycode == 0x37 && ctrl)
  {
    if (pressed)
    {
      JsonDocument &app = status();
      if (app["screen"].as<int>() == MENUSCREEN)
      {
        app["menu"]["state"] = MENU_HOME;
        Home_focusCards(true); // already in the menu: just cross the divider
        Menu_clear();          // mark dirty so the menu repaints
      }
      else
      {
        app["menu"]["goto"] = MENU_SETTINGS;
        app["screen"] = MENUSCREEN;
      }
    }
    return;
  }

  // Ctrl + A/C/X/V/Z/Y: select-all / copy / cut / paste / undo / redo (mirror
  // Fn + the same letters on the keypad). Ctrl+Shift+Z is an alias for redo.
  // Emitted once, on press, as action codes the word processor handles.
  if (ctrl && status()["screen"].as<int>() == WORDPROCESSOR &&
      (keycode == 0x04 || keycode == 0x06 || keycode == 0x1b || keycode == 0x19 ||
       keycode == 0x1d || keycode == 0x1c))
  {
    if (pressed)
    {
      int action = keycode == 0x04 ? SELECTALL
                   : keycode == 0x06 ? COPY
                   : keycode == 0x1b ? CUT
                   : keycode == 0x19 ? PASTE
                   : keycode == 0x1d ? (shift ? REDO : UNDO) // Z / Shift+Z
                                     : REDO;                 // Y
      display_keyboard(action, true, keycode);
    }
    return;
  }

  // Translate the Keycode to ASCII
  JsonDocument &app = status();
  // Text-entry contexts type in the configured layout; every other screen
  // forces US so Latin shortcut keys always match regardless of layout (a
  // non-Latin layout would otherwise map the shortcuts to letters the menu
  // can't match, stranding the user). The Rename field is a text-entry screen
  // even though it lives under the menu, so it honours the real layout - that's
  // how an Arabic custom title gets typed.
  int curScreen = app["screen"].as<int>();
  bool textEntry = (curScreen == WORDPROCESSOR) ||
                   (curScreen == MENUSCREEN && app["menu"]["state"].as<int>() == MENU_RENAME);
  String locale = textEntry ? app["config"]["keyboard_layout"].as<String>()
                            : String("US");

  // Some layouts have keys that expand to two code points (Windows Arabic
  // lam-alef: ل + ا). The single-value path below can't carry that, so emit the
  // pair on key-down and swallow the release.
  int c1 = 0, c2 = 0;
  if (keyboard_locale_ligature(locale, keycode, shift, &c1, &c2))
  {
    if (pressed)
    {
      display_keyboard(c1, true, keycode);
      display_keyboard(c1, false, keycode);
      display_keyboard(c2, true, keycode);
      display_keyboard(c2, false, keycode);
    }
    return;
  }

  // int: non-Latin layouts return Unicode code points above 255
  int ascii = keyboard_keycode_ascii(locale, keycode, shift, alt, pressed);
  if (ascii != 0)
    // send key to GUI
    display_keyboard(ascii, pressed, keycode);
}