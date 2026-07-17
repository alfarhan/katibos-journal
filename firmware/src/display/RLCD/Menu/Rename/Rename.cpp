#include "Rename.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

#include "service/Editor/Editor.h"
#include "service/Buffer/BufferService.h"
#include "service/Tools/TextUtil.h"
#include "service/Bidi/Bidi.h"
#include "service/Clock/Clock.h"

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

    Menu_drawHeader(display, u8, "Rename file");

    const int xl = 20;
    u8->setFont(u8g2_font_profont17_tf);

    // field label
    u8->setCursor(xl, 70);
    u8->print("New title");

    // input box — a clear text field so it's obvious where typing lands
    const int bx = xl, by = 84, bw = 360, bh = 32;
    display->drawRectangle(bx, by, bx + bw, by + bh, 1);
    int caretX = RLCD_drawShapedLabel(u8, bx + 10, by + 22, capUtf8(buffer_get(), 30).c_str(), false);
    u8->setFont(u8g2_font_profont17_tf);
    u8->drawGlyph(bx + 10 + caretX, by + 22, '_');

    // secondary hint
    u8->setCursor(xl, 150);
    u8->print("Leave blank to auto-name from line 1.");

    // current title, for reference
    String current = app["config"][format("title_%d", fi)].as<String>();
    if (!current.isEmpty() && current != "null")
    {
        u8->setCursor(xl, 190);
        u8->print("Current:  ");
        RLCD_drawShapedLabel(u8, u8->getCursorX(), 190, capUtf8(current, 22).c_str(), false);
    }

    display->drawLine(0, 276, 400, 276, 1);
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(xl, 296);
    u8->print("[ENT] save   [ESC] cancel");
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
            app["config"][format("title_%d", fi)] = capUtf8(t, 28);
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

    // Ctrl+D: insert the date+time into the title (same source as the editor).
    // Only when a date is known (string starts with a digit, not "----------").
    else if (key == DATE_INSERT)
    {
        String d = clock_datestr();
        if (d.length() >= 10 && d[0] >= '0' && d[0] <= '9')
            for (int i = 0; i < (int)d.length(); i++)
                buffer_add(d[i]);
    }

    // Printable character (ASCII or any layout's Unicode codepoint). Mirror the
    // editor's text rule: nav codes are < 32, command codes live in 1000..1199;
    // real characters are ASCII (< 1000) or Arabic and beyond (>= 1536).
    else if (key >= 32 && key != 127 && (key < 1000 || key > 1199) && key <= 0xFFFF)
    {
        char enc[4];
        int len = bidi::utf8Encode((uint16_t)key, enc);
        for (int i = 0; i < len; i++)
            buffer_add(enc[i]);
    }

    Menu_clear();
}
