#include "ar.h"

// HID keycode -> Arabic code point. Index is the USB HID Usage ID (0x00..0x7F).
// Letter positions follow the macOS Arabic layout (Bishoy/Mac-Ar-Layout-for-Win).
static const uint16_t AR_BASE[128] = {
    // 0x00..0x03
    0, 0, 0, 0,
    /*0x04 a*/ 0x0634, /*0x05 b*/ 0x0632, /*0x06 c*/ 0x0630, /*0x07 d*/ 0x064A,
    /*0x08 e*/ 0x062B, /*0x09 f*/ 0x0628, /*0x0a g*/ 0x0644, /*0x0b h*/ 0x0627,
    /*0x0c i*/ 0x0647, /*0x0d j*/ 0x062A, /*0x0e k*/ 0x0646, /*0x0f l*/ 0x0645,
    /*0x10 m*/ 0x0648, /*0x11 n*/ 0x0631, /*0x12 o*/ 0x062E, /*0x13 p*/ 0x062D,
    /*0x14 q*/ 0x0636, /*0x15 r*/ 0x0642, /*0x16 s*/ 0x0633, /*0x17 t*/ 0x0641,
    /*0x18 u*/ 0x0639, /*0x19 v*/ 0x062F, /*0x1a w*/ 0x0635, /*0x1b x*/ 0x0637,
    /*0x1c y*/ 0x063A, /*0x1d z*/ 0x0638,
    /*0x1e 1*/ '1', /*0x1f 2*/ '2', /*0x20 3*/ '3', /*0x21 4*/ '4', /*0x22 5*/ '5',
    /*0x23 6*/ '6', /*0x24 7*/ '7', /*0x25 8*/ '8', /*0x26 9*/ '9', /*0x27 0*/ '0',
    /*0x28 enter*/ '\n', /*0x29 esc*/ 0, /*0x2a bksp*/ '\b', /*0x2b tab*/ '\t',
    /*0x2c space*/ ' ', /*0x2d -*/ '-', /*0x2e =*/ '=',
    /*0x2f [*/ 0x062C, /*0x30 ]*/ 0x0629, /*0x31 backslash*/ 0x005C, /*0x32*/ 0,
    /*0x33 ;*/ 0x0643, /*0x34 '*/ 0x061B, /*0x35 `*/ 0x00A7,
    /*0x36 ,*/ 0x060C, /*0x37 .*/ '.', /*0x38 /*/ '/',
    // remainder zero
};

// Shift layer (macOS Arabic): harakat on the top row, «»/madda/alef-maqsura on
// the home row, hamza forms on the bottom row, Latin symbols on the number row.
// 0 means "fall back to the base letter".
static const uint16_t AR_SHIFT[128] = {
    0, 0, 0, 0,
    /*a*/ 0x00AB /*«*/, /*b*/ 0x0623 /*أ*/, /*c*/ 0x0626 /*ئ*/, /*d*/ 0x0649 /*ى*/,
    /*e*/ 0x0650 /*kasra*/, /*f*/ 0, /*g*/ 0, /*h*/ 0x0622 /*آ*/,
    /*i*/ 0x0651 /*shadda*/, /*j*/ 0, /*k*/ 0, /*l*/ 0,
    /*m*/ 0x0624 /*ؤ*/, /*n*/ 0x0625 /*إ*/, /*o*/ 0x005B /*[*/, /*p*/ 0x005D /*]*/,
    /*q*/ 0x064E /*fatha*/, /*r*/ 0x064D /*kasratan*/, /*s*/ 0x00BB /*»*/, /*t*/ 0x064F /*damma*/,
    /*u*/ 0x0652 /*sukun*/, /*v*/ 0x0621 /*hamza*/, /*w*/ 0x064B /*fathatan*/, /*x*/ 0,
    /*y*/ 0x064C /*dammatan*/, /*z*/ 0,
    /*1*/ '!', /*2*/ '@', /*3*/ '#', /*4*/ '$', /*5*/ '%',
    /*6*/ '^', /*7*/ '&', /*8*/ '*', /*9*/ ')', /*0*/ '(',
    /*enter*/ '\n', 0, '\b', '\t', ' ', /*-*/ '_', /*=*/ '+',
    /*[*/ 0x007B /*{*/, /*]*/ 0x007D /*}*/, /*backslash*/ 0x007C /*|*/, 0,
    /*;*/ ':', /*'*/ '"', /*`*/ 0x00B1 /*±*/,
    /*,*/ '<', /*.*/ '>', /*/ */ 0x061F /*؟*/,
};

int keyboard_keycode_ascii_ar(uint8_t keycode, bool shift, bool alt, bool pressed)
{
    if (keycode >= 128)
        return 0;

    // AltGr layer: bidi/joining controls on the number row (macOS Arabic).
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
        cp = AR_SHIFT[keycode];
    if (cp == 0)
        cp = AR_BASE[keycode];

    return cp;
}
