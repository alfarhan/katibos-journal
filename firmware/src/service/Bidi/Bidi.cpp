#include "Bidi.h"

namespace bidi
{
    // ===================== UTF-8 =====================

    int utf8CharLen(uint8_t lead)
    {
        if (lead < 0x80) return 1;
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        return 1; // continuation byte or invalid -> treat as single (lenient)
    }

    int utf8Encode(uint16_t cp, char *out)
    {
        if (cp < 0x80)
        {
            out[0] = (char)cp;
            return 1;
        }
        if (cp < 0x800)
        {
            out[0] = (char)(0xC0 | (cp >> 6));
            out[1] = (char)(0x80 | (cp & 0x3F));
            return 2;
        }
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    int utf8DecodeOne(const char *s, int byteLen, uint16_t *cp)
    {
        uint8_t b0 = (uint8_t)s[0];
        if (b0 < 0x80)
        {
            *cp = b0;
            return 1;
        }
        if ((b0 & 0xE0) == 0xC0 && byteLen >= 2 && ((uint8_t)s[1] & 0xC0) == 0x80)
        {
            *cp = ((b0 & 0x1F) << 6) | ((uint8_t)s[1] & 0x3F);
            return 2;
        }
        if ((b0 & 0xF0) == 0xE0 && byteLen >= 3 &&
            ((uint8_t)s[1] & 0xC0) == 0x80 && ((uint8_t)s[2] & 0xC0) == 0x80)
        {
            *cp = ((b0 & 0x0F) << 12) | (((uint8_t)s[1] & 0x3F) << 6) | ((uint8_t)s[2] & 0x3F);
            return 3;
        }
        // lenient: legacy Latin-1 / invalid -> pass the raw byte through as a
        // codepoint so old single-byte files still render something.
        *cp = b0;
        return 1;
    }

    // ===================== Arabic shaping data =====================

    // Joining types.
    enum JoinType { JT_U = 0, JT_R, JT_D, JT_C, JT_T };

    // For letters 0x0621..0x064A: the four presentation forms (Forms-B) and the
    // joining type. forms = { isolated, final, initial, medial }; 0 = none.
    struct LetterForms { uint16_t forms[4]; uint8_t jt; };

    static const uint16_t ARABIC_FIRST = 0x0621;
    static const uint16_t ARABIC_LAST = 0x064A;

    static const LetterForms LETTERS[] = {
        /*0621 HAMZA*/            {{0xFE80, 0, 0, 0}, JT_U},
        /*0622 ALEF MADDA*/       {{0xFE81, 0xFE82, 0, 0}, JT_R},
        /*0623 ALEF HAMZA ABOVE*/ {{0xFE83, 0xFE84, 0, 0}, JT_R},
        /*0624 WAW HAMZA*/        {{0xFE85, 0xFE86, 0, 0}, JT_R},
        /*0625 ALEF HAMZA BELOW*/ {{0xFE87, 0xFE88, 0, 0}, JT_R},
        /*0626 YEH HAMZA*/        {{0xFE89, 0xFE8A, 0xFE8B, 0xFE8C}, JT_D},
        /*0627 ALEF*/             {{0xFE8D, 0xFE8E, 0, 0}, JT_R},
        /*0628 BEH*/              {{0xFE8F, 0xFE90, 0xFE91, 0xFE92}, JT_D},
        /*0629 TEH MARBUTA*/      {{0xFE93, 0xFE94, 0, 0}, JT_R},
        /*062A TEH*/              {{0xFE95, 0xFE96, 0xFE97, 0xFE98}, JT_D},
        /*062B THEH*/             {{0xFE99, 0xFE9A, 0xFE9B, 0xFE9C}, JT_D},
        /*062C JEEM*/             {{0xFE9D, 0xFE9E, 0xFE9F, 0xFEA0}, JT_D},
        /*062D HAH*/              {{0xFEA1, 0xFEA2, 0xFEA3, 0xFEA4}, JT_D},
        /*062E KHAH*/             {{0xFEA5, 0xFEA6, 0xFEA7, 0xFEA8}, JT_D},
        /*062F DAL*/              {{0xFEA9, 0xFEAA, 0, 0}, JT_R},
        /*0630 THAL*/             {{0xFEAB, 0xFEAC, 0, 0}, JT_R},
        /*0631 REH*/              {{0xFEAD, 0xFEAE, 0, 0}, JT_R},
        /*0632 ZAIN*/             {{0xFEAF, 0xFEB0, 0, 0}, JT_R},
        /*0633 SEEN*/             {{0xFEB1, 0xFEB2, 0xFEB3, 0xFEB4}, JT_D},
        /*0634 SHEEN*/            {{0xFEB5, 0xFEB6, 0xFEB7, 0xFEB8}, JT_D},
        /*0635 SAD*/              {{0xFEB9, 0xFEBA, 0xFEBB, 0xFEBC}, JT_D},
        /*0636 DAD*/              {{0xFEBD, 0xFEBE, 0xFEBF, 0xFEC0}, JT_D},
        /*0637 TAH*/              {{0xFEC1, 0xFEC2, 0xFEC3, 0xFEC4}, JT_D},
        /*0638 ZAH*/              {{0xFEC5, 0xFEC6, 0xFEC7, 0xFEC8}, JT_D},
        /*0639 AIN*/              {{0xFEC9, 0xFECA, 0xFECB, 0xFECC}, JT_D},
        /*063A GHAIN*/            {{0xFECD, 0xFECE, 0xFECF, 0xFED0}, JT_D},
        /*063B*/ {{0, 0, 0, 0}, JT_U}, /*063C*/ {{0, 0, 0, 0}, JT_U},
        /*063D*/ {{0, 0, 0, 0}, JT_U}, /*063E*/ {{0, 0, 0, 0}, JT_U},
        /*063F*/ {{0, 0, 0, 0}, JT_U},
        /*0640 TATWEEL*/          {{0x0640, 0x0640, 0x0640, 0x0640}, JT_C},
        /*0641 FEH*/              {{0xFED1, 0xFED2, 0xFED3, 0xFED4}, JT_D},
        /*0642 QAF*/              {{0xFED5, 0xFED6, 0xFED7, 0xFED8}, JT_D},
        /*0643 KAF*/              {{0xFED9, 0xFEDA, 0xFEDB, 0xFEDC}, JT_D},
        /*0644 LAM*/              {{0xFEDD, 0xFEDE, 0xFEDF, 0xFEE0}, JT_D},
        /*0645 MEEM*/             {{0xFEE1, 0xFEE2, 0xFEE3, 0xFEE4}, JT_D},
        /*0646 NOON*/             {{0xFEE5, 0xFEE6, 0xFEE7, 0xFEE8}, JT_D},
        /*0647 HEH*/              {{0xFEE9, 0xFEEA, 0xFEEB, 0xFEEC}, JT_D},
        /*0648 WAW*/              {{0xFEED, 0xFEEE, 0, 0}, JT_R},
        /*0649 ALEF MAKSURA*/     {{0xFEEF, 0xFEF0, 0, 0}, JT_R},
        /*064A YEH*/              {{0xFEF1, 0xFEF2, 0xFEF3, 0xFEF4}, JT_D},
    };

    static const LetterForms *lookup(uint16_t cp)
    {
        if (cp < ARABIC_FIRST || cp > ARABIC_LAST) return nullptr;
        return &LETTERS[cp - ARABIC_FIRST];
    }

    // Arabic diacritics (harakat) are transparent: they do not break joining.
    static bool isTransparent(uint16_t cp)
    {
        return (cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 ||
               (cp >= 0x06D6 && cp <= 0x06ED);
    }

    bool isArabicLetter(uint16_t cp) { return lookup(cp) != nullptr; }

    bool isArabic(uint16_t cp)
    {
        return (cp >= 0x0600 && cp <= 0x06FF) || // base block
               (cp >= 0xFB50 && cp <= 0xFDFF) || // presentation forms-A
               (cp >= 0xFE70 && cp <= 0xFEFF);   // presentation forms-B
    }

    static bool isAlefVariant(uint16_t cp)
    {
        return cp == 0x0627 || cp == 0x0622 || cp == 0x0623 || cp == 0x0625;
    }

    // Brackets are mirrored when displayed in a right-to-left run.
    static uint16_t mirrorGlyph(uint16_t c)
    {
        switch (c)
        {
        case '(': return ')'; case ')': return '(';
        case '[': return ']'; case ']': return '[';
        case '{': return '}'; case '}': return '{';
        case '<': return '>'; case '>': return '<';
        case 0x00AB: return 0x00BB; case 0x00BB: return 0x00AB; // guillemets
        }
        return c;
    }

    // Lam-alef ligature codepoint for a given alef, final form or isolated.
    static uint16_t lamAlef(uint16_t alef, bool finalForm)
    {
        switch (alef)
        {
        case 0x0622: return finalForm ? 0xFEF6 : 0xFEF5; // madda
        case 0x0623: return finalForm ? 0xFEF8 : 0xFEF7; // hamza above
        case 0x0625: return finalForm ? 0xFEFA : 0xFEF9; // hamza below
        default:     return finalForm ? 0xFEFC : 0xFEFB; // plain alef
        }
    }

    // ===================== internal logical buffer =====================

    namespace
    {
        const int MAXN = 80; // a single screen line never exceeds this

        const int MAXMARKS = 3;

        struct Run
        {
            uint16_t cp;       // logical codepoint (lam for a lam-alef pair)
            uint16_t glyph;    // shaped glyph to draw
            int byteStart;
            int byteLen;
            uint8_t level;     // bidi embedding level
            bool arabic;
            bool lamAlef;      // this run is a lam-alef ligature
            uint16_t alefCp;   // the alef variant, when lamAlef
            uint16_t marks[3]; // combining harakat folded onto this base
            uint8_t nmarks;
        };
    }

    // can `cur` (jt) connect to the letter on its right (previous in logical)?
    static bool canJoinRight(uint8_t jt) { return jt == JT_R || jt == JT_D; }
    // can `cur` connect to the letter on its left (next in logical)?
    static bool canJoinLeft(uint8_t jt) { return jt == JT_D || jt == JT_C; }
    // can a letter accept a join coming from its right side?
    static bool acceptsFromRight(uint8_t jt) { return jt == JT_R || jt == JT_D || jt == JT_C; }

    int layoutLine(const char *line, int byteLen, Cell *out, int maxCells,
                   bool *lineIsRTL, bool baseHintRTL)
    {
        Run runs[MAXN];
        int n = 0;

        // ---- 1. decode + lam-alef ligation (logical order) ----
        int i = 0;
        while (i < byteLen && n < MAXN)
        {
            uint16_t cp;
            int len = utf8DecodeOne(line + i, byteLen - i, &cp);

            // A combining mark (haraka) folds onto the preceding base letter so
            // it overlays it at zero advance instead of taking its own cell.
            if (isTransparent(cp) && n > 0)
            {
                Run &base = runs[n - 1];
                if (base.nmarks < MAXMARKS)
                    base.marks[base.nmarks++] = cp;
                base.byteLen += len; // the mark's bytes belong to the base cell
                i += len;
                continue;
            }

            runs[n].cp = cp;
            runs[n].glyph = cp;
            runs[n].byteStart = i;
            runs[n].byteLen = len;
            runs[n].level = 0;
            runs[n].arabic = isArabic(cp);
            runs[n].lamAlef = false;
            runs[n].alefCp = 0;
            runs[n].nmarks = 0;

            // lam followed by an alef variant -> single ligature cell
            if (cp == 0x0644 && (i + len) < byteLen)
            {
                uint16_t nextCp;
                int nlen = utf8DecodeOne(line + i + len, byteLen - i - len, &nextCp);
                if (isAlefVariant(nextCp))
                {
                    runs[n].lamAlef = true;
                    runs[n].alefCp = nextCp;
                    runs[n].byteLen = len + nlen;
                    runs[n].glyph = lamAlef(nextCp, false); // iso/final decided in shaping
                    n++;
                    i += len + nlen;
                    continue;
                }
            }

            n++;
            i += len;
        }

        // ---- 2. Arabic shaping (choose joining forms) ----
        for (int k = 0; k < n; k++)
        {
            // a lam-alef pair shapes as lam (dual-joining) for context purposes
            uint16_t baseCp = runs[k].lamAlef ? 0x0644 : runs[k].cp;
            const LetterForms *cur = lookup(baseCp);
            if (!cur) continue; // not a shaping letter (incl. transparent marks)

            // nearest previous shaping letter (skip transparent marks).
            // A lam-alef's LEFT side is the alef (right-joining), so for the
            // "does prev join forward" test it must be treated as the alef, not
            // the lam - otherwise the following letter wrongly connects to it.
            const LetterForms *prev = nullptr;
            for (int p = k - 1; p >= 0; p--)
            {
                if (isTransparent(runs[p].cp)) continue;
                prev = lookup(runs[p].lamAlef ? runs[p].alefCp : runs[p].cp);
                break;
            }
            // nearest next shaping letter (skip transparent marks)
            const LetterForms *next = nullptr;
            for (int q = k + 1; q < n; q++)
            {
                if (isTransparent(runs[q].cp)) continue;
                next = lookup(runs[q].lamAlef ? 0x0644 : runs[q].cp);
                break;
            }

            // joinsPrev: cur connects rightward to prev, and prev connects on its left
            bool joinsPrev = prev && canJoinRight(cur->jt) && canJoinLeft(prev->jt);
            // joinsNext: cur connects leftward to next, and next accepts from its right.
            // NB: a lam-alef's left side is the alef (right-joining) -> never joins next.
            bool joinsNext = !runs[k].lamAlef && next &&
                             canJoinLeft(cur->jt) && acceptsFromRight(next->jt);

            if (runs[k].lamAlef)
            {
                // lam-alef: isolated vs final (final when it joins a previous letter)
                runs[k].glyph = lamAlef(runs[k].alefCp, joinsPrev);
                continue;
            }

            int form;
            if (joinsPrev && joinsNext) form = 3;      // medial
            else if (joinsPrev)         form = 1;      // final
            else if (joinsNext)         form = 2;      // initial
            else                        form = 0;      // isolated

            uint16_t g = cur->forms[form];
            if (g == 0) g = cur->forms[1]; // fall back to final
            if (g == 0) g = cur->forms[0]; // then isolated
            if (g == 0) g = baseCp;        // then the bare letter
            runs[k].glyph = g;
        }

        // ---- 3. bidi: classify, resolve base direction + embedding levels ----

        // class per run: 0 = strong L (latin), 1 = strong R (arabic letter),
        // 2 = number, 3 = neutral.
        uint8_t cls[MAXN];
        for (int k = 0; k < n; k++)
        {
            uint16_t c = runs[k].lamAlef ? 0x0644 : runs[k].cp;
            // Arabic-Indic digits are "Arabic Numbers": left-to-right.
            bool arabicDigit = (c >= 0x0660 && c <= 0x0669) || (c >= 0x06F0 && c <= 0x06F9);
            if (arabicDigit || (c >= '0' && c <= '9')) cls[k] = 2;
            else if (isArabic(c)) cls[k] = 1;
            else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                     (c >= 0x00C0 && c <= 0x024F)) cls[k] = 0;
            else cls[k] = 3;
        }

        // number-adjacent symbols (% $ + - . , : / and the Arabic percent etc.)
        // join an adjacent number so "50%", "$50", "2024/06/20" stay one LTR run.
        for (int k = 0; k < n; k++)
        {
            uint16_t c = runs[k].lamAlef ? 0x0644 : runs[k].cp;
            bool et = c == '%' || c == '$' || c == '+' || c == '-' || c == '.' ||
                      c == ',' || c == ':' || c == '/' || c == '#' ||
                      c == 0x066A || c == 0x066B || c == 0x066C || c == 0x2030 ||
                      c == 0x00A3 || c == 0x20AC || c == 0x00A5;
            if (!et) continue;
            if ((k > 0 && cls[k - 1] == 2) || (k < n - 1 && cls[k + 1] == 2))
                cls[k] = 2;
        }

        // base direction = first strong character (Unicode rule P2/P3),
        // falling back to the caller's hint when there is none.
        bool baseRTL = baseHintRTL;
        for (int k = 0; k < n; k++)
        {
            if (cls[k] == 1) { baseRTL = true; break; }
            if (cls[k] == 0) { baseRTL = false; break; }
        }
        if (lineIsRTL) *lineIsRTL = baseRTL;
        uint8_t baseLevel = baseRTL ? 1 : 0;

        // embedding level from class. Numbers/Latin are level 0 in an LTR base
        // (no reordering) but level 2 (an LTR island) inside an RTL base.
        for (int k = 0; k < n; k++)
        {
            if (cls[k] == 1) runs[k].level = 1;                 // arabic -> RTL
            else if (cls[k] == 0) runs[k].level = baseRTL ? 2 : 0;
            else if (cls[k] == 2) runs[k].level = baseRTL ? 2 : 0;
            else runs[k].level = 255;                           // neutral
        }

        // resolve neutrals: same level on both sides -> that level, else base.
        for (int k = 0; k < n; k++)
        {
            if (runs[k].level != 255) continue;
            int p = k - 1; while (p >= 0 && runs[p].level == 255) p--;
            int q = k + 1; while (q < n && runs[q].level == 255) q++;
            uint8_t lp = (p >= 0) ? runs[p].level : baseLevel;
            uint8_t lq = (q < n) ? runs[q].level : baseLevel;
            runs[k].level = (lp == lq) ? lp : baseLevel;
        }

        // ---- 4. reorder to visual order (UBA rule L2) ----
        int order[MAXN];
        for (int k = 0; k < n; k++) order[k] = k;

        uint8_t maxLevel = baseLevel;
        uint8_t minOdd = 255;
        for (int k = 0; k < n; k++)
        {
            if (runs[k].level > maxLevel) maxLevel = runs[k].level;
            if ((runs[k].level & 1) && runs[k].level < minOdd) minOdd = runs[k].level;
        }
        if (baseLevel & 1) minOdd = baseLevel < minOdd ? baseLevel : minOdd;
        if (minOdd == 255) minOdd = (baseLevel & 1) ? baseLevel : 1;

        for (int lvl = maxLevel; lvl >= minOdd; lvl--)
        {
            int k = 0;
            while (k < n)
            {
                if (runs[order[k]].level >= lvl)
                {
                    int s = k;
                    while (k < n && runs[order[k]].level >= lvl) k++;
                    // reverse order[s..k-1]
                    for (int a = s, b = k - 1; a < b; a++, b--)
                    {
                        int t = order[a]; order[a] = order[b]; order[b] = t;
                    }
                }
                else k++;
            }
        }

        // ---- 5. emit cells in visual order ----
        int count = 0;
        for (int v = 0; v < n && count < maxCells; v++)
        {
            Run &r = runs[order[v]];
            // brackets/quotes in an RTL run display mirrored
            out[count].glyph = (r.level & 1) ? mirrorGlyph(r.glyph) : r.glyph;
            out[count].arabic = r.arabic;
            out[count].byteStart = r.byteStart;
            out[count].byteLen = r.byteLen;
            out[count].nmarks = r.nmarks;
            for (int m = 0; m < r.nmarks; m++)
                out[count].marks[m] = r.marks[m];
            count++;
        }
        return count;
    }
}
