#include "Rename.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

#include "service/Editor/Editor.h"
#include "service/Buffer/BufferService.h"
#include "service/Tools/TextUtil.h"
#include "service/Bidi/Bidi.h"

// The title is what sync uses as the remote filename (title + ".txt"), so this
// is a filename limit, not just a display one.
static const int TITLE_MAX = 64;
static const int CARET_W = 10; // the '_' drawn after the text

static int utf8Len(const char *s)
{
    int n = 0;
    for (; *s; s++)
        if (((unsigned char)*s & 0xC0) != 0x80)
            n++;
    return n;
}

// The field shows the TAIL of a long title, clipped by measured width rather
// than a character count - typing here is append-only, so the end is the part
// you need to see, and an Arabic glyph is not a Latin one's width.
static String fieldTail(U8G2_FOR_ST73XX *u8, const char *s, int avail)
{
    String t = s;
    while (t.length() && RLCD_shapedLabelWidth(u8, t.c_str(), false) > avail)
    {
        int k = 1;
        while (k < (int)t.length() && ((unsigned char)t[k] & 0xC0) == 0x80)
            k++;
        t = t.substring(k);
    }
    return t;
}

void Rename_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // Start from an empty field; the current title is shown for reference and a
    // blank save reverts to the auto title. (Editing in place would need full
    // UTF-8 caret handling in the small entry buffer - kept simple for now.)
    buffer_clear();
    Menu_clear();
}

void Rename_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();
    int fi = app["config"]["file_index"].as<int>();

    // the rename prompt is a dialog, so it carries its own title bar
    RLCD_drawWindow(display, u8, 12, 44, 376, 176, "RENAME FILE");

    const int xl = 32;
    u8->setFont(u8g2_font_profont17_tf);

    // field label
    u8->setCursor(xl, 96);
    u8->print("New title");

    // input box — a clear text field so it's obvious where typing lands
    const int bx = xl, by = 106, bw = 336, bh = 32;
    display->drawRectangle(bx, by, bx + bw, by + bh, 1);
    int caretX = RLCD_drawShapedLabel(u8, bx + 10, by + 22,
                                      fieldTail(u8, buffer_get(), bw - 20 - CARET_W).c_str(), false);
    u8->setFont(u8g2_font_profont17_tf);
    u8->drawGlyph(bx + 10 + caretX, by + 22, '_');

    // secondary hint
    u8->setCursor(xl, 172);
    u8->print("Leave blank to auto-name from line 1.");

    // current title, for reference
    String current = app["config"][format("title_%d", fi)].as<String>();
    if (!current.isEmpty() && current != "null")
    {
        u8->setCursor(xl, 202);
        u8->print("Current:  ");
        RLCD_drawShapedLabel(u8, u8->getCursorX(), 202, capUtf8(current, 22).c_str(), false);
    }

    u8->setFont(u8g2_font_profont17_tf);
    static const RLCD_Hint HINTS[] = {{"ENT", "SAVE"}, {"ESC", "CANCEL"}};
    RLCD_drawHintBar(display, u8, 12, 252, RLCD_HINTS(HINTS));
}

void Rename_keyboard(int key)
{
    JsonDocument &app = status();
    int fi = app["config"]["file_index"].as<int>();

    // ENTER: commit
    if (key == '\n' || key == '\r')
    {
        String t = String(buffer_get());
        t.trim();
        if (t.isEmpty())
        {
            // revert to the auto title (first non-empty line of the file)
            app["config"][format("title_manual_%d", fi)] = false;
            app["config"][format("title_%d", fi)] = deriveTitle(Editor::getInstance().fileHead(512));
        }
        else
        {
            app["config"][format("title_manual_%d", fi)] = true;
            app["config"][format("title_%d", fi)] = capUtf8(t, TITLE_MAX);
        }
        config_save();
        buffer_clear();
        app["menu"]["state"] = MENU_HOME;
    }

    // ESC / MENU: cancel without saving
    else if (key == 27 || key == MENU)
    {
        buffer_clear();
        app["menu"]["state"] = MENU_HOME;
    }

    // BACKSPACE: remove one whole UTF-8 character (not a single byte)
    else if (key == '\b' || key == 127)
    {
        char *b = buffer_get();
        int n = (int)strlen(b);
        if (n > 0)
        {
            int k = n - 1;
            while (k > 0 && ((unsigned char)b[k] & 0xC0) == 0x80)
                k--;
            for (int r = 0; r < n - k; r++)
                buffer_remove();
        }
    }

    // Printable character (ASCII or any layout's Unicode codepoint). Mirror the
    // editor's text rule: nav codes are < 32, command codes live in 1000..1199;
    // real characters are ASCII (< 1000) or Arabic and beyond (>= 1536).
    else if (key >= 32 && key != 127 && (key < 1000 || key > 1199) && key <= 0xFFFF)
    {
        if (utf8Len(buffer_get()) >= TITLE_MAX)
        {
            Menu_clear(); // full: refuse the key rather than cut it off at save
            return;
        }
        char enc[4];
        int len = bidi::utf8Encode((uint16_t)key, enc);
        for (int i = 0; i < len; i++)
            buffer_add(enc[i]);
    }

    Menu_clear();
}
