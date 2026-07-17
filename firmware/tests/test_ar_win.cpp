#include "microtest.h"
#include "keyboard/Locale/ar_win/ar_win.h"

// HID usage IDs for the physical keys we probe.
enum { K_A=0x04, K_B=0x05, K_G=0x0a, K_M=0x10, K_N=0x11, K_Q=0x14, K_T=0x17,
       K_1=0x1e, K_RBRACK=0x30, K_SEMI=0x33, K_QUOTE=0x34, K_GRAVE=0x35,
       K_SLASH=0x38 };

static int base(uint8_t kc)  { return keyboard_keycode_ascii_ar_win(kc, false, false, true); }
static int shft(uint8_t kc)  { return keyboard_keycode_ascii_ar_win(kc, true,  false, true); }
static int altg(uint8_t kc)  { return keyboard_keycode_ascii_ar_win(kc, false, true,  true); }

// ---- base letters (shared with the standard Arabic arrangement) ----
TEST(arw_base_letters) {
    CHECK_EQ_INT(base(K_Q), 0x0636); // ض
    CHECK_EQ_INT(base(K_A), 0x0634); // ش
    CHECK_EQ_INT(base(K_M), 0x0629); // ة  teh marbuta
    CHECK_EQ_INT(base(K_N), 0x0649); // ى  alef maqsura
}

// ---- keys that DIFFER from the macOS layout ----
TEST(arw_differs_from_mac) {
    CHECK_EQ_INT(base(K_QUOTE),  0x0637); // ' -> ط  (Mac: ؛)
    CHECK_EQ_INT(base(K_RBRACK), 0x062F); // ] -> د  (Mac: ة)
    CHECK_EQ_INT(base(K_GRAVE),  0x0630); // ` -> ذ
    CHECK_EQ_INT(base(K_SLASH),  0x0638); // / -> ظ
    CHECK_EQ_INT(shft(K_SLASH),  0x061F); // shift+/ -> ؟
    CHECK_EQ_INT(base(K_SEMI),   0x0643); // ; -> ك
}

// ---- shift layer: harakat + shadda on backtick ----
TEST(arw_shift_harakat) {
    CHECK_EQ_INT(shft(K_A),     0x0650); // kasra
    CHECK_EQ_INT(shft(K_Q),     0x064E); // fatha
    CHECK_EQ_INT(shft(K_GRAVE), 0x0651); // shadda
}

// ---- AltGr: bidi/joining controls on the number row ----
TEST(arw_altgr_bidi) {
    CHECK_EQ_INT(altg(K_1), 0x200D); // ZWJ
}

// ---- lam-alef ligature keys expand to TWO code points ----
TEST(arw_ligatures) {
    int c1 = 0, c2 = 0;

    // 'b' base -> لا
    CHECK(keyboard_ar_win_ligature(K_B, false, &c1, &c2));
    CHECK_EQ_INT(c1, 0x0644); CHECK_EQ_INT(c2, 0x0627);
    // 'b' shift -> لآ
    CHECK(keyboard_ar_win_ligature(K_B, true, &c1, &c2));
    CHECK_EQ_INT(c1, 0x0644); CHECK_EQ_INT(c2, 0x0622);
    // shift+'g' -> لأ ; base 'g' is plain lam, not a ligature
    CHECK(keyboard_ar_win_ligature(K_G, true, &c1, &c2));
    CHECK_EQ_INT(c1, 0x0644); CHECK_EQ_INT(c2, 0x0623);
    CHECK(!keyboard_ar_win_ligature(K_G, false, &c1, &c2));
    CHECK_EQ_INT(base(K_G), 0x0644); // ل
    // shift+'t' -> لإ ; base 't' is plain feh
    CHECK(keyboard_ar_win_ligature(K_T, true, &c1, &c2));
    CHECK_EQ_INT(c1, 0x0644); CHECK_EQ_INT(c2, 0x0625);
    CHECK(!keyboard_ar_win_ligature(K_T, false, &c1, &c2));
    CHECK_EQ_INT(base(K_T), 0x0641); // ف

    // the ligature keys return 0 from the single-value path (so the pair path wins)
    CHECK_EQ_INT(base(K_B), 0);
    CHECK_EQ_INT(shft(K_B), 0);
}
