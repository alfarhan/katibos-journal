#pragma once

//
#include <Arduino.h>

// Screen Type
#define WORDPROCESSOR 0

//
#define ERRORSCREEN 1
#define MENUSCREEN 2
#define UPDATESCREEN 6

// menu id. Sparse on purpose: these are runtime state values, never persisted,
// and screens that no longer exist left their numbers behind. Reusing a gap is
// fine; renumbering the live ones buys nothing.
#define MENU_HOME 0
#define MENU_SYNC 1
#define MENU_CLEAR 2
#define MENU_LAYOUT 3
#define MENU_WIFI 4
#define MENU_STORAGE 12
#define MENU_RENAME 16
#define MENU_SETTINGS 17
#define MENU_HELP 18
#define MENU_ABOUT 20
#define MENU_UPDATE 21
#define MENU_SYNCPROV 23
#define MENU_PREFS 24
#define MENU_FACTORY 26

// special key
#define MENU 0x6

// keyboard action codes routed to the active screen as a key (not text input)
#define SAVE 1100
#define STATUSBAR 1101

// selection-extend nav codes + clipboard actions (don't collide with ASCII,
// control codes, file change 1000-1010, SAVE/STATUSBAR)
#define SEL_LEFT 1102
#define SEL_RIGHT 1103
#define SEL_UP 1104
#define SEL_DOWN 1105
#define SEL_HOME 1106
#define SEL_END 1107
#define SEL_WORD_LEFT 1108
#define SEL_WORD_RIGHT 1109
#define SELECTALL 1110
#define COPY 1111
#define CUT 1112
#define PASTE 1113
#define UNDO 1114
#define REDO 1115

// jump caret to document start / end (Ctrl+Home / Ctrl+End), paging if needed
#define DOC_TOP 1116
#define DOC_BOTTOM 1117

// trigger a background sync from the editor (Ctrl+U); progress shows in the
// status bar without leaving the editor
#define SYNC 1118

// jump caret to the previous / next word without selecting (Ctrl+Left / Ctrl+Right)
#define WORD_LEFT 1119
#define WORD_RIGHT 1120

// insert today's date (YYYY-MM-DD) at the caret (Ctrl+D)

// jump caret to the previous / next paragraph (Ctrl+Up / Ctrl+Down)
#define PARA_UP 1122
#define PARA_DOWN 1123

// sync ALL files (Ctrl+Shift+U) — works from the editor and the menu screens.
// SYNC (Ctrl+U) syncs only the currently open file.
#define SYNC_ALL 1124

// open the editor shortcut overlay (Ctrl+/ or Ctrl+?) — any key returns to the file
#define HELP_KEY 1125

// jump from the editor straight to Preferences (Ctrl+,). Esc there comes back to
// the editor rather than the menu, via menu.prefs_from_editor. Once described an
// in-editor options panel that no longer exists; the code was left defined but
// unhandled when that panel folded into Preferences.
#define OPTIONS_KEY 1126

// jump from the editor to the settings cards (Ctrl+.) — same one-shot menu.goto
// route as OPTIONS_KEY, landing on the cards half of MENU_HOME
#define CARDS_KEY 1128

// send the open file to the AI speller and replace it with the result (Ctrl+G)
#define AI_PROOFREAD 1127

//
void display_setup();
void display_loop();
int display_core(); // show which core to run display routine

//
void display_keyboard(int key, bool pressed, int index = -1);
