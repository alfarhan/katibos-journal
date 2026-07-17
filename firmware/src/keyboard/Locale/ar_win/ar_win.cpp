#include "ar_win.h"

// HID keycode -> Arabic code point for the standard Windows "Arabic 101"
// layout (KBDA1). Index is the USB HID Usage ID (0x00..0x7F).
// Verified against Microsoft's KBDA1 layout data (kbdlayout.info).
static const uint16_t ARW_BASE[128] = {
    // 0x00..0x03
    0, 0, 0, 0,
    /*0x04 a*/ 0x0634, /*0x05 b*/ 0 /*لا ligature*/, /*0x06 c*/ 0x0624, /*0x07 d*/ 0x064A,
    /*0x08 e*/ 0x062B, /*0x09 f*/ 0x0628, /*0x0a g*/ 0x0644, /*0x0b h*/ 0x0627,
    /*0x0c i*/ 0x0647, /*0x0d j*/ 0x062A, /*0x0e k*/ 0x0646, /*0x0f l*/ 0x0645,
    /*0x10 m*/ 0x0629, /*0x11 n*/ 0x0649, /*0x12 o*/ 0x062E, /*0x13 p*/ 0x062D,
    /*0x14 q*/ 0x0636, /*0x15 r*/ 0x0642, /*0x16 s*/ 0x0633, /*0x17 t*/ 0x0641,
    /*0x18 u*/ 0x0639, /*0x19 v*/ 0x0631, /*0x1a w*/ 0x0635, /*0x1b x*/ 0x0621,
    /*0x1c y*/ 0x063A, /*0x1d z*/ 0x0626,
    /*0x1e 1*/ '1', /*0x1f 2*/ '2', /*0x20 3*/ '3', /*0x21 4*/ '4', /*0x22 5*/ '5',
    /*0x23 6*/ '6', /*0x24 7*/ '7', /*0x25 8*/ '8', /*0x26 9*/ '9', /*0x27 0*/ '0',
    /*0x28 enter*/ '\n', /*0x29 esc*/ 0, /*0x2a bksp*/ '\b', /*0x2b tab*/ '\t',
    /*0x2c space*/ ' ', /*0x2d -*/ '-', /*0x2e =*/ '=',
    /*0x2f [*/ 0x062C, /*0x30 ]*/ 0x062F, /*0x31 backslash*/ 0x005C, /*0x32*/ 0,
    /*0x33 ;*/ 0x0643, /*0x34 '*/ 0x0637, /*0x35 `*/ 0x0630,
    /*0x36 ,*/ 0x0648, /*0x37 .*/ 0x0632, /*0x38 /*/ 0x0638,
    // remainder zero
};

// Shift layer (Windows Arabic 101): harakat on the top-row letters, hamza forms
// and brackets on the home/bottom rows, Latin symbols on the number row, Arabic
// punctuation (، ؛ ؟) scattered per the standard. 0 falls back to the base char.
static const uint16_t ARW_SHIFT[128] = {
    0, 0, 0, 0,
    /*a*/ 0x0650 /*kasra*/, /*b*/ 0 /*لآ ligature*/, /*c*/ 0x007D /*}*/, /*d*/ 0x005D /*]*/,
    /*e*/ 0x064F /*damma*/, /*f*/ 0x005B /*[*/, /*g*/ 0 /*لأ ligature*/, /*h*/ 0x0623 /*أ*/,
    /*i*/ 0x00F7 /*÷*/, /*j*/ 0x0640 /*tatweel*/, /*k*/ 0x060C /*،*/, /*l*/ 0x002F /*/*/,
    /*m*/ 0x2019 /*’*/, /*n*/ 0x0622 /*آ*/, /*o*/ 0x00D7 /*×*/, /*p*/ 0x061B /*؛*/,
    /*q*/ 0x064E /*fatha*/, /*r*/ 0x064C /*dammatan*/, /*s*/ 0x064D /*kasratan*/, /*t*/ 0 /*لإ ligature*/,
    /*u*/ 0x2018 /*‘*/, /*v*/ 0x007B /*{*/, /*w*/ 0x064B /*fathatan*/, /*x*/ 0x0652 /*sukun*/,
    /*y*/ 0x0625 /*إ*/, /*z*/ 0x007E /*~*/,
    /*1*/ '!', /*2*/ '@', /*3*/ '#', /*4*/ '$', /*5*/ '%',
    /*6*/ '^', /*7*/ '&', /*8*/ '*', /*9*/ ')', /*0*/ '(',
    /*enter*/ '\n', 0, '\b', '\t', ' ', /*-*/ '_', /*=*/ '+',
    /*[*/ 0x003C /*<*/, /*]*/ 0x003E /*>*/, /*backslash*/ 0x007C /*|*/, 0,
    /*;*/ 0x003A /*:*/, /*'*/ 0x0022 /*"*/, /*`*/ 0x0651 /*shadda*/,
    /*,*/ ',', /*.*/ '.', /*/ */ 0x061F /*؟*/,
};

bool keyboard_ar_win_ligature(uint8_t keycode, bool shift, int *c1, int *c2)
{
    switch (keycode)
    {
    case 0x05: // 'b' : لا (base) / لآ (shift)
        *c1 = 0x0644;
        *c2 = shift ? 0x0622 : 0x0627;
        return true;
    case 0x0a: // 'g' + shift : لأ  (base 'g' is plain ل)
        if (shift) { *c1 = 0x0644; *c2 = 0x0623; return true; }
        return false;
    case 0x17: // 't' + shift : لإ  (base 't' is plain ف)
        if (shift) { *c1 = 0x0644; *c2 = 0x0625; return true; }
        return false;
    default:
        return false;
    }
}

int keyboard_keycode_ascii_ar_win(uint8_t keycode, bool shift, bool alt, bool pressed)
{
    if (keycode >= 128)
        return 0;

    // AltGr layer: bidi/joining controls on the number row (same as macOS Arabic).
    if (alt)
    {
        switch (keycode)
        {
        case 0x1e: return 0x200D; // 1 -> ZWJ
        case 0x1f: return 0x200C; // 2 -> ZWNJ
        case 0x20: return 0x200E; // 3 -> LRM
        case 0x21: return 0x200F; // 4 -> RLM
        default: break;
        }
    }

    uint16_t cp = 0;
    if (shift)
        cp = ARW_SHIFT[keycode];
    if (cp == 0)
        cp = ARW_BASE[keycode];

    return cp;
}
