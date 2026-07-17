#include "WordProcessor.h"
#include "app/app.h"
#include "keyboard/keyboard.h"
#include "keyboard/Locale/locale.h"
#include "display/display.h"
#include "display/RLCD/display_RLCD.h"

//
#include "service/Editor/Editor.h"
#include "service/Editor/EditorViewport.h"
#include "service/Bidi/Bidi.h"
#include "service/Tools/TextUtil.h"
#include "service/Sync/Sync.h"
#include "service/Clock/Clock.h"
#include "app/Config/Config.h"
#include "display/RLCD/Menu/Help/Help.h"
#include "display/RLCD/Menu/Menu.h"
#include <string.h>

// Arabic glyphs are drawn from a fixed-width font that carries the connected
// presentation forms; Latin/ASCII keeps the existing ProFont.
#define WP_FONT_ARABIC u8g2_font_10x20_t_arabic

//
int STATUSBAR_Y = 295;
int screen_width = 400;
int screen_height = 300;
bool clear_background = true;
unsigned int last_sleep = millis();

// When the status bar is hidden (Ctrl+H / Fn+H) the screen is distraction-free;
// a save still flashes "SAVED" until saved_flash_until so the user gets
// confirmation, then it disappears again.
bool statusbar_hidden = false;
unsigned int saved_flash_until = 0;

// Boot crash-recovery prompt: when a write-ahead snapshot survived an unclean
// shutdown, the editor is frozen behind a modal until the writer picks Enter
// (restore the unsaved text) or Esc (discard it, keeping the last clean save).
// Painted once (single full SPI push) rather than every tick.
bool recovery_prompt = false;
static bool recovery_painted = false;

// editor shortcut overlay (Ctrl+/) — a modal drawn over the page
static bool help_overlay = false;
static bool help_painted = false;
void WP_render_recovery(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8);


// Text alignment (config "text_align"). AUTO follows each line's base direction
// (RTL right, LTR left); JUSTIFY stretches inter-word spaces to fill the width
// on soft-wrapped lines. Read once per frame into wp_align by WP_render_text and
// shared with WP_render_line / WP_render_cursor.
enum { ALIGN_AUTO = 0, ALIGN_RIGHT = 1, ALIGN_LEFT = 2, ALIGN_JUSTIFY = 3 };
static int wp_align = ALIGN_AUTO;

// Screen Y baseline of the active (caret) text line, set each frame by
// WP_render_text from the scroll mode and shared with WP_render_cursor so the
// blink bar tracks the caret instead of being pinned to the bottom.
static int caretBaselineY = 0;

// Whether the last WP_render() actually changed anything visible - the
// caller uses this to decide whether the (expensive, full 30KB SPI) panel
// refresh is worth doing this tick.
static bool needsDisplay = true;
bool WP_contentChanged();

//
const int font_width = 12;
const int font_height = 22;
// Bottom anchor of the text region. Mutable: when the status bar is hidden the
// anchor drops to reclaim the bar's band for an extra line (applyStatusbarLayout).
int cursorY = 270;
const int cursorHeight = 2;
const int marginX = 5;
//
int editY = cursorY - 6;

#define WP_FONT u8g2_font_profont22_mf

// Line spacing: the baseline-to-baseline pitch. Glyphs stay font_height (22)
// tall; the pitch only changes the gap. Compact/Tight go BELOW font_height to
// fit more lines - fine for unvocalized Arabic, but harakat/tashkīl can touch.
static int linePitchFor(int spacing)
{
    switch (spacing)
    {
    case 1: return 26;           // Relaxed
    case 2: return 30;           // Spacious
    case 3: return 20;           // Compact
    default: return font_height; // Normal (22)
    }
}

// Line-spacing codes ordered tight -> loose for the options cycle, so Left/Right
// step through them naturally (the stored code keeps Normal=0 as the safe default).
static const int spacingCycle[] = {3, 0, 1, 2}; // Compact, Normal, Relaxed, Spacious

// Apply line spacing to the editor's row count — the single source of truth for
// lines-per-screen, also used by Page Up/Down. Called at setup and whenever the
// spacing changes in the options panel.
static void applyLineSpacing(int spacing)
{
    Editor::getInstance().rows = editY / linePitchFor(spacing);
}

// Hidden status bar -> reclaim its band for one more text line. Drops the bottom
// anchor (cursorY/editY) and recomputes rows. The bottom-mode caret-bar offset is
// (cursorY - editY), so keeping that delta at 6 keeps the bar hugging its line
// wherever the anchor lands. Shown = the legacy 270/264 (pixel-identical). Hidden
// editY=292: the bottom line sits flush to the edge (~1px) and the leftover slack
// stays at the top (~13px); still 13 lines (292/22).
static void applyStatusbarLayout()
{
    cursorY = statusbar_hidden ? 298 : 270;
    editY = cursorY - 6;
    applyLineSpacing(status()["config"]["line_spacing"] | 0);
}

// u8g2 handle the editor's wrap-width callback can use to measure glyphs.
static U8G2_FOR_ST73XX *wp_u8 = nullptr;
static int WP_cell_width(U8G2_FOR_ST73XX *u8, const bidi::Cell &cell);

// Pixel advance of one logical code point in isolation. Used as the fallback
// width; for Arabic, contextual shaping is handled by WP_measure_char_at below.
// Combining marks are zero-advance (they overlay their base letter).
static int WP_measure_char(uint16_t cp)
{
    if ((cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED))
        return 0; // harakat / combining marks
    if (!wp_u8)
        return font_width;

    // ASCII/Latin: ProFont is monospace, so one cached value covers it.
    if (cp < 0x0600)
    {
        static int latinW = 0;
        if (latinW == 0)
        {
            wp_u8->setFont(WP_FONT);
            latinW = wp_u8->getUTF8Width("n");
            if (latinW <= 0)
                latinW = font_width;
        }
        return latinW;
    }

    // Arabic base block: isolated-form width per code point. NOTE this differs
    // from the shaped (connected) form actually drawn - WP_measure_char_at does
    // the contextual measurement for wrapping; this stays only as a fallback.
    if (cp >= 0x0600 && cp <= 0x06FF)
    {
        static int8_t cache[256];
        static bool init = false;
        if (!init)
        {
            for (int k = 0; k < 256; k++)
                cache[k] = -1;
            init = true;
        }
        int idx = cp - 0x0600;
        if (cache[idx] < 0)
        {
            wp_u8->setFont(WP_FONT_ARABIC);
            char b[4];
            int len = bidi::utf8Encode(cp, b);
            b[len] = 0;
            int w = wp_u8->getUTF8Width(b);
            if (w < 0) w = 0;
            if (w > 30) w = 30;
            cache[idx] = (int8_t)w;
        }
        return cache[idx];
    }

    // anything else (presentation forms, symbols): measure directly
    wp_u8->setFont(WP_FONT_ARABIC);
    char b[4];
    int len = bidi::utf8Encode(cp, b);
    b[len] = 0;
    int w = wp_u8->getUTF8Width(b);
    return (w > 0) ? w : font_width;
}

// Contextual wrap measurement: the advance of the char at byte `i`, shaped the
// SAME way WP_render_line draws it (bidi::layoutLine + WP_cell_width), so a
// full Arabic line's wrap width matches its rendered width exactly (the
// isolated forms WP_measure_char returns are narrower than the connected forms,
// which made lines over-pack and overflow the right margin).
//
// Joining only acts within a word, so we shape the whole word (run between
// spaces/newlines) once and cache it - the per-keystroke rewrap then shapes
// each word a single time, not once per character.
static int WP_measure_char_at(const char *buf, int i, int len)
{
    uint16_t cp;
    bidi::utf8DecodeOne(&buf[i], len - i, &cp);

    // harakat / combining marks: folded onto their base when rendered (0 advance)
    if ((cp >= 0x064B && cp <= 0x065F) || cp == 0x0670 || (cp >= 0x06D6 && cp <= 0x06ED))
        return 0;
    // non-Arabic (Latin, digits, punctuation, space): no contextual shaping
    if (!bidi::isArabic(cp))
        return WP_measure_char(cp);

    static const char *cw_buf = nullptr;
    static int cw_ws = -1, cw_we = -1;
    static int cw_w[96];
    if (!(buf == cw_buf && i >= cw_ws && i < cw_we))
    {
        int ws = i;
        while (ws > 0 && buf[ws - 1] != ' ' && buf[ws - 1] != '\n')
            ws--;
        int we = i;
        while (we < len && buf[we] && buf[we] != ' ' && buf[we] != '\n')
            we++;
        if (we - ws > 90) // pathological no-space run: skip shaping
            return WP_measure_char(cp);

        static bidi::Cell cells[96];
        bool rtl = false;
        int n = bidi::layoutLine(&buf[ws], we - ws, cells, 96, &rtl, true);
        for (int k = 0; k < we - ws; k++)
            cw_w[k] = 0;
        for (int c = 0; c < n; c++)
            if (cells[c].byteStart >= 0 && cells[c].byteStart < we - ws)
                cw_w[cells[c].byteStart] = WP_cell_width(wp_u8, cells[c]);
        cw_buf = buf;
        cw_ws = ws;
        cw_we = we;
    }
    return cw_w[i - cw_ws];
}

//
void WP_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // wire pixel-accurate wrapping into the editor before it loads any file
    wp_u8 = u8;
    Editor::getInstance().measureCharWidth = WP_measure_char;
    Editor::getInstance().measureCharWidthAt = WP_measure_char_at;
    Editor::getInstance().wrapWidthPx = screen_width - 2 * marginX - font_width;

    // editor instantiate
    Editor::getInstance().init(34, 12);

    // setup default color
    JsonDocument &app = status();

    // restore the persistent status-bar default (Ctrl+H still toggles per session)
    statusbar_hidden = app["config"]["statusbar_hidden"] | false;

    // restore the line spacing + status-bar layout (sets rows-per-screen and the
    // bottom anchor; when the bar boots hidden the text reclaims its band)
    applyStatusbarLayout();

    // load file from the editor
    int file_index = app["config"]["file_index"].as<int>();
    String path = format("/%d.txt", file_index);

    // crash recovery, before loading: (A) heal a save the previous session may
    // have left half-spliced, then (B) if an unsaved-edit snapshot survived,
    // raise the restore/discard prompt over the cleanly loaded file.
    Editor::getInstance().salvageInterruptedSave(path);
    Editor::getInstance().loadFile(path);
    recovery_prompt = Editor::getInstance().recoveryPending();
    recovery_painted = false;

    // start from clear background
    clear_background = true;
    display->clearDisplay();
    display->display();

    // sleep timer reset
    last_sleep = millis();
}

//
void WP_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // Apply the theme via the panel's hardware inversion. It's a sticky register
    // (persists across all screens once set), so applying it here — the first and
    // main screen — covers the whole UI. Only re-sent when the setting changes.
    static int applied_theme = -1;
    int wantDark = status()["config"]["theme_dark"] | 0;
    if (wantDark != applied_theme)
    {
        display->display_Inversion(wantDark != 0);
        applied_theme = wantDark;
    }

    // Modal crash-recovery prompt takes over the screen until answered. Paint
    // it once, then leave the panel untouched (no per-tick 30KB refresh).
    if (recovery_prompt)
    {
        if (!recovery_painted)
        {
            WP_render_recovery(display, u8);
            recovery_painted = true;
            needsDisplay = true;
        }
        else
            needsDisplay = false;
        return;
    }

    // Editor shortcut overlay (Ctrl+/): paint once, hold until a key dismisses it.
    if (help_overlay)
    {
        if (!help_painted)
        {
            Help_render_editor(display, u8);
            help_painted = true;
            needsDisplay = true;
        }
        else
            needsDisplay = false;
        return;
    }


    // A single-file sync (Ctrl+U) that pulled a newer remote version flags a
    // reload of the open file. Apply it once, and only if the user hasn't typed
    // since (Editor.saved) so live edits are never overwritten; then clear it.
    {
        JsonDocument &app = status();
        int reload = app["sync_reload"] | -1;
        if (reload >= 0 && app["sync_state"].as<int>() == SYNC_COMPLETED &&
            reload == app["config"]["file_index"].as<int>())
        {
            if (Editor::getInstance().saved)
            {
                Editor::getInstance().loadFile(format("/%d.txt", reload));
                Editor::getInstance().pageChanged = true; // force a full redraw
            }
            app["sync_reload"] = -1; // one-shot: applied or skipped
        }
    }

    // the editor swapped to a different window of the file (paging, or the
    // buffer filling up while typing) - force a full redraw
    if (Editor::getInstance().pageChanged)
    {
        Editor::getInstance().pageChanged = false;
        display->clearDisplay();
        clear_background = true;
    }

    // timers
    WP_check_saved();

    // When the selection changes (extended, collapsed, cleared), scrollback
    // lines need to repaint with/without their highlight bars - force a full
    // redraw so stale highlight never lingers. Track anchor + caret together.
    {
        static int selAnchor_prev = -2;
        static int selCursor_prev = -2;
        int selAnchor = Editor::getInstance().selAnchor;
        int selCursor = Editor::getInstance().hasSelection() ? Editor::getInstance().cursorPos : -1;
        if (selAnchor != selAnchor_prev || selCursor != selCursor_prev)
        {
            // wipe the frame so stale highlight bars never linger (the clear path
            // in WP_render_clear assumes the caller already cleared the panel).
            display->clearDisplay();
            clear_background = true;
            selAnchor_prev = selAnchor;
            selCursor_prev = selCursor;
        }
    }

    // CLEAR BACKGROUND
    WP_render_clear(display, u8);

    // RENDER TEXT
    WP_render_text(display, u8);

    // RENDER STATUS BAR
    WP_render_status(display, u8);

    // BLINK CURSOR
    WP_render_cursor(display, u8);

    // decide whether the panel actually needs to be pushed over SPI this tick
    needsDisplay = WP_contentChanged();

    //
    if (clear_background)
        clear_background = false;

    // Editor House Keeping Task
    Editor::getInstance().loop();

}

bool WP_needsDisplay()
{
    return needsDisplay;
}

// Compares the values that actually drive what's on screen against the
// last time this was checked. The draw calls above always run (they just
// write into the in-RAM frame buffer, which is cheap) - this only decides
// whether the buffer is worth pushing over SPI to the physical panel.
bool WP_contentChanged()
{
    static int cursorPos_prev = -1;
    static int cursorLinePos_prev = -1;
    static int bufferSize_prev = -1;
    static bool saved_prev = true;
    static int wordCount_prev = -1;
    static int file_index_prev = -1;
    static int blinkPhase_prev = -1;
    static int selAnchor_prev = -2;
    static String layout_prev = "";

    JsonDocument &app = status();

    String layout = app["config"]["keyboard_layout"].as<String>();
    int selAnchor = Editor::getInstance().selAnchor;
    int cursorPos = Editor::getInstance().cursorPos;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int bufferSize = Editor::getInstance().getBufferSize();
    bool saved = Editor::getInstance().saved;
    int wordCount = Editor::getInstance().wordCountFile + Editor::getInstance().wordCountBuffer;
    int file_index = app["config"]["file_index"].as<int>();
    // flips every 500ms, matching WP_render_cursor()'s blink rate
    int blinkPhase = (millis() / 500) % 2;

    bool changed = clear_background ||
                   cursorPos != cursorPos_prev ||
                   cursorLinePos != cursorLinePos_prev ||
                   bufferSize != bufferSize_prev ||
                   saved != saved_prev ||
                   wordCount != wordCount_prev ||
                   file_index != file_index_prev ||
                   blinkPhase != blinkPhase_prev ||
                   selAnchor != selAnchor_prev ||
                   layout != layout_prev;

    selAnchor_prev = selAnchor;
    cursorPos_prev = cursorPos;
    cursorLinePos_prev = cursorLinePos;
    bufferSize_prev = bufferSize;
    saved_prev = saved;
    wordCount_prev = wordCount;
    file_index_prev = file_index;
    blinkPhase_prev = blinkPhase;
    layout_prev = layout;

    return changed;
}

// Check if text is saved
void WP_check_saved()
{
    //
    static unsigned int last = millis();
    static int lastBufferSize = Editor::getInstance().getBufferSize();
    int bufferSize = Editor::getInstance().getBufferSize();

    //
    // when the file is saved then extend the autosave timer
    if (lastBufferSize != bufferSize)
    {
        last = millis();

        //
        lastBufferSize = bufferSize;
    }

    //
    // when idle for 4 seconds then auto save
    if (millis() - last > 2000)
    {
        //
        last = millis();

        if (!Editor::getInstance().saved)
            Editor::getInstance().saveFile();
    }
}

//
// Clear Screen
// Do it as less as possible so that there is the least amount of the flicker
//
void WP_render_clear(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // clear background
    if (clear_background)
    {
        // screen buffer will be pushed after
        return;
    }

    //
    JsonDocument &app = status();

    //
    static int cursorLine_prev = 0;
    static int cursorPos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int cursorLineLength = Editor::getInstance().lineLengths[cursorLine];

    //
    static int bufferSize_prev = 0;
    int bufferSize = Editor::getInstance().getBufferSize();

    // When new line clear everything
    if (cursorLine_prev != cursorLine)
    {
        //
        clear_background = true;

        //
        cursorLine_prev = cursorLine;
    }

    // When Backspace, trailing characters should be deleted
    // if it is backspace or del key
    if (cursorPos_prev >= cursorPos && bufferSize_prev != bufferSize)

        clear_background = true;

    if (cursorPos_prev != cursorPos)
    {
        // if it is typing at the end don't flicker
        if (cursorPos != Editor::getInstance().getBufferSize())
        {
            // if the edit line is empty then don't flicker
            //
            if (cursorLinePos + 1 != cursorLineLength)
                clear_background = true;
        }

        //
        cursorPos_prev = cursorPos;
    }

    if (bufferSize != bufferSize_prev)
    {
        bufferSize_prev = bufferSize;
    }

    //
    if (clear_background)
    {
        // clear the screen buffer so the next render will get the clean
        display->clearDisplay();
    }
}

// Byte span of a line, with any trailing newline trimmed.
static int WP_line_bytes(int line_num, char **start_out)
{
    Editor &ed = Editor::getInstance();
    char *start = ed.linePositions[line_num];
    int byteLen = (line_num < ed.totalLine)
                      ? (int)(ed.linePositions[line_num + 1] - start)
                      : (int)strlen(start);
    while (byteLen > 0 && start[byteLen - 1] == '\n')
        byteLen--;
    *start_out = start;
    return byteLen;
}

// Is the active layout Arabic? Used so an empty line defaults to RTL.
static bool WP_layout_rtl()
{
    JsonDocument &app = status();
    return keyboard_locale_is_arabic(app["config"]["keyboard_layout"].as<String>());
}

// Lay a line out into visual cells (shaped + bidi-reordered).
static int WP_layout(int line_num, bidi::Cell *cells, int maxCells, bool *rtl)
{
    char *start;
    int byteLen = WP_line_bytes(line_num, &start);
    // base direction follows the first strong character; for blank or
    // punctuation/number-only lines, fall back to the active layout.
    return bidi::layoutLine(start, byteLen, cells, maxCells, rtl, WP_layout_rtl());
}

// Pixel advance of one cell's glyph. Arabic is proportional (so the connected
// forms actually touch); Latin's ProFont is monospace so this is constant.
static int WP_cell_width(U8G2_FOR_ST73XX *u8, const bidi::Cell &cell)
{
    u8->setFont(cell.arabic ? WP_FONT_ARABIC : WP_FONT);
    char b[4];
    int len = bidi::utf8Encode(cell.glyph, b);
    b[len] = 0;
    return u8->getUTF8Width(b);
}

// Leftmost x for a line of pixel width `total` under the given alignment. AUTO
// follows the base direction; RIGHT/LEFT force an edge. Never past the margin.
static int WP_align_x(int align, bool rtl, int total)
{
    int x;
    if (align == ALIGN_RIGHT)
        x = (screen_width - marginX) - total;
    else if (align == ALIGN_LEFT)
        x = marginX;
    else // AUTO (and the non-justify fallback for JUSTIFY)
        x = rtl ? ((screen_width - marginX) - total) : marginX;
    if (x < marginX)
        x = marginX;
    return x;
}

//
// Draw one editor line at the baseline currently set on `u8` (via setCursor).
// Arabic is shaped, the whole line reordered for bidi, and glyphs advanced by
// their real width so Arabic joins up. Alignment follows wp_align.
void WP_render_line(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8, int line_num)
{
    int y = u8->getCursorY();

    // static (not stack): these render fns are only ever called from the one
    // display loop, never reentrantly.
    static bidi::Cell cells[80];
    bool rtl = false;
    int n = WP_layout(line_num, cells, 80, &rtl);

    // total pixel width, to align the line
    int total = 0;
    for (int c = 0; c < n; c++)
        total += WP_cell_width(u8, cells[c]);

    // Justify only soft-wrapped lines (they end in a space; paragraph-final
    // lines don't) and not the caret line (keeps the caret math simple). Other
    // modes use a fixed alignment. Spaces between words carry the extra width.
    char *rawb; int rawn = WP_line_bytes(line_num, &rawb);
    bool justifyLine = (wp_align == ALIGN_JUSTIFY && rawn > 0 && rawb[rawn - 1] == ' ' &&
                        line_num != Editor::getInstance().cursorLine);
    int gaps = 0, extraBase = 0, extraRem = 0;
    if (justifyLine)
    {
        for (int c = 1; c < n - 1; c++)
            if (!cells[c].arabic && cells[c].glyph == ' ')
                gaps++;
        int extra = (screen_width - 2 * marginX) - total;
        if (gaps > 0 && extra > 0)
        {
            extraBase = extra / gaps;
            extraRem = extra % gaps;
        }
        else
            justifyLine = false;
    }

    int x = justifyLine ? marginX
                        : WP_align_x(wp_align == ALIGN_JUSTIFY ? ALIGN_AUTO : wp_align, rtl, total);

    // Selection range in absolute buffer bytes, and this line's byte offset, so
    // each visual cell can be tested for membership. This works for both LTR and
    // RTL lines because cells already carry x/width in visual order.
    Editor &ed = Editor::getInstance();
    bool sel = ed.hasSelection();
    int selS = sel ? ed.selStart() : 0;
    int selE = sel ? ed.selEnd() : 0;
    int lineStartAbs = (int)(ed.linePositions[line_num] - ed.linePositions[0]);
    // vertical band of the highlight, matching the edit-line band clear
    int hiTop = y - font_height + 4;
    int hiBot = y + 6;

    for (int c = 0; c < n; c++)
    {
        int w = WP_cell_width(u8, cells[c]);

        bool cellSelected = false;
        if (sel)
        {
            int absByte = lineStartAbs + cells[c].byteStart;
            cellSelected = (absByte >= selS && absByte < selE);
        }

        if (cellSelected)
        {
            // black bar under the cell, glyph drawn white on top. The font is in
            // opaque mode (setFontMode(0)), so its per-glyph background box would
            // paint white over the bar - flip the background to black too.
            display->drawFilledRectangle(x, hiTop, x + w, hiBot, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
        }

        u8->setFont(cells[c].arabic ? WP_FONT_ARABIC : WP_FONT);
        u8->drawGlyph(x, y, cells[c].glyph);

        // overlay combining harakat centered over the base, no advance
        for (int m = 0; m < cells[c].nmarks; m++)
        {
            u8->setFont(WP_FONT_ARABIC);
            char b[4];
            int bl = bidi::utf8Encode(cells[c].marks[m], b);
            b[bl] = 0;
            int mw = u8->getUTF8Width(b);
            u8->drawGlyph(x + (w - mw) / 2, y, cells[c].marks[m]);
        }

        if (cellSelected)
        {
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
        }

        x += w;
        // stretch interior inter-word gaps to fill the line when justifying
        if (justifyLine && c >= 1 && c < n - 1 && !cells[c].arabic && cells[c].glyph == ' ')
        {
            x += extraBase;
            if (extraRem > 0) { x += 1; extraRem--; }
        }
    }

    // restore the Latin font + colors for any callers that draw after
    u8->setFont(WP_FONT);
    u8->setForegroundColor(ST7305_COLOR_BLACK);
    u8->setBackgroundColor(ST7305_COLOR_WHITE);
}

//
void WP_render_text(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // SET FONT
    u8->setFont(WP_FONT);

    // Cursor Information
    static int cursorLine_prev = 0;
    static int cursorLinePos_prev = 0;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int totalLine = Editor::getInstance().totalLine;

    // Scroll mode decides where the active line sits and which file line is at
    // the top of the view. Read once per frame (cheap). Default = bottom-anchored,
    // so behaviour is identical to before unless the writer opts in via Ctrl+,.
    int mode = app["config"]["scroll_mode"] | SCROLL_BOTTOM;
    wp_align = app["config"]["text_align"] | ALIGN_AUTO; // shared with line/cursor render
    int linePitch = linePitchFor(app["config"]["line_spacing"] | 0);
    int rows = Editor::getInstance().rows; // lines per screen (set from line spacing)
    // Bottom-most usable screen row. The top baseline sits one pitch below y=0
    // (the y=0 slot would clip the line), so there are `rows` rows: 0..rows-1.
    int lastRow = rows - 1;

    static int topLine_persist = 0;
    int topLine = editorTopLine(mode, cursorLine, lastRow, topLine_persist);
    topLine_persist = topLine;

    int caretRow = cursorLine - topLine;
    if (caretRow < 0) caretRow = 0;
    if (caretRow > lastRow) caretRow = lastRow;

    // Each row's baseline is measured UP from editY by the line pitch, so bottom
    // mode at Normal spacing is pixel-identical to the previous fixed layout.
    int caretY = editY - (lastRow - caretRow) * linePitch;
    caretBaselineY = caretY; // shared with WP_render_cursor

    // Full repaint: draw every visible row except the caret line (drawn last).
    if (clear_background)
    {
        u8->setFont(WP_FONT);
        for (int r = 0; r <= lastRow; r++)
        {
            if (r == caretRow)
                continue;
            int line = topLine + r;
            if (line < 0 || line >= totalLine)
                continue;
            u8->setCursor(marginX, editY - (lastRow - r) * linePitch);
            WP_render_line(display, u8, line);
        }
    }

    //
    // The caret line is the edit area, redrawn every tick. When we are NOT doing
    // a full-screen clear, the right-aligned RTL line shifts and reshapes as it
    // grows, so painting over the previous frame leaves ghost glyphs behind.
    // Clear just this line's band (one glyph-box tall) and repaint it; also
    // repaint the line above, whose descenders dip into the cleared band.
    if (!clear_background)
    {
        display->drawFilledRectangle(0, caretY - font_height, screen_width, caretY + 6, 0);
        if (caretRow - 1 >= 0 && cursorLine - 1 >= 0)
        {
            u8->setCursor(marginX, caretY - linePitch);
            WP_render_line(display, u8, cursorLine - 1);
        }
    }

    u8->setCursor(marginX, caretY);
    WP_render_line(display, u8, cursorLine);
}


// Status Bar
// - file index
// - current file size
// - keyboard layout
// - saved status
void WP_render_status(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // The hidden-bar "saved" toast flashes only on a MANUAL save (Ctrl+S sets
    // saved_flash_until); auto-saves stay silent so they don't keep interrupting
    // a distraction-free session.

    // Hidden bar: the text reclaims this band, so do NOT clear it every tick
    // (that would erase the bottom line). Instead show transient notifications as
    // an inverted toast (black box / white text) at the TOP-LEFT so they stay
    // legible over the text and clear of the caret (which sits at the bottom in
    // the default flow): a sync's progress + result (Ctrl+U), and a brief SAVED
    // after a save. When nothing is pending, force one repaint to wipe the last
    // toast and restore the text underneath.
    if (statusbar_hidden)
    {
        static bool toastShown = false;
        static unsigned int sync_toast_until = 0;
        int sync_state = app["sync_state"].as<int>();

        String toast;
        if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
        {
            sync_toast_until = 0; // still running
            String msg = app["sync_message"].as<String>();
            toast = msg.isEmpty() ? String("syncing...") : capUtf8(msg, 30);
        }
        else if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
        {
            if (sync_toast_until == 0)
                sync_toast_until = millis() + 3000; // hold the result ~3s
            if (millis() < sync_toast_until)
            {
                if (sync_state == SYNC_ERROR)
                {
                    String err = app["sync_error"].as<String>(); err.trim();
                    toast = err.isEmpty() ? String("sync failed") : capUtf8(err, 30);
                }
                else
                {
                    String msg = app["sync_message"].as<String>(); msg.trim();
                    toast = msg.isEmpty() ? String("synced") : capUtf8(msg, 30);
                }
            }
        }

        // no sync notification active -> fall back to the save flash
        if (toast.isEmpty() && millis() < saved_flash_until)
            toast = "saved";

        static int prevBoxW = 0;
        if (!toast.isEmpty())
        {
            u8->setFont(u8g2_font_profont17_tf);
            int tw = u8->getUTF8Width(toast.c_str());
            const int pad = 5;
            const int boxBottom = 24; // top strip
            int boxW = tw + pad * 2;
            // Clear ONLY the toast's own area (the wider of this/last frame, so a
            // shrinking toast leaves no black tail) - the rest of the first line
            // must stay visible. Then paint an opaque black box with white text.
            display->drawFilledRectangle(0, 0, boxW > prevBoxW ? boxW : prevBoxW, boxBottom, 0);
            display->drawFilledRectangle(0, 0, boxW, boxBottom, 1);
            u8->setForegroundColor(ST7305_COLOR_WHITE);
            u8->setBackgroundColor(ST7305_COLOR_BLACK);
            u8->setCursor(pad, 18);
            u8->print(toast.c_str());
            u8->setForegroundColor(ST7305_COLOR_BLACK);
            u8->setBackgroundColor(ST7305_COLOR_WHITE);
            prevBoxW = boxW;
            toastShown = true;
        }
        else if (toastShown)
        {
            toastShown = false;
            prevBoxW = 0;
            Editor::getInstance().pageChanged = true; // repaint to clear the toast
        }
        return;
    }

    // Visible bar: clear its band first - the fields below are redrawn every tick
    // without moving, so a value that changes width (e.g. word count 9 -> 10)
    // would otherwise leave ghost glyphs ("14WWORDS") under the new text.
    display->drawFilledRectangle(0, STATUSBAR_Y - 16, screen_width, screen_height, 0);

    // STATUS BAR
    display->drawLine(0, STATUSBAR_Y - 16, screen_width, STATUSBAR_Y - 16, 1);

    // FILE INDEX + TITLE
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(5, STATUSBAR_Y);
    {
        int fi = app["config"]["file_index"].as<int>();
        String title = app["config"][format("title_%d", fi)].as<String>();
        if (title.isEmpty() || title == "null")
        {
            u8->printf("untitled");
        }
        else
        {
            // title only (no file index); shaping helper so a (possibly Arabic)
            // title isn't blank in the Latin-only status font.
            RLCD_drawShapedLabel(u8, u8->getCursorX(), STATUSBAR_Y, capUtf8(title, 9).c_str(), false);
        }
    }

    // STATUS GROUP: one contiguous "saved | xx words | ar" string, right-aligned
    // flush to the edge (file name stays on the left). The save token is a fixed
    // 5 wide so toggling saved<->dots never changes the width. All-Latin, LTR.
    String locale = app["config"]["keyboard_layout"].as<String>();
    String locLabel = (locale.isEmpty() || locale == "null" || locale == "US") ? "en" : locale;
    locLabel.toLowerCase();

    String savedTok = Editor::getInstance().saved ? "saved" : " ...."; // 4 dots, 5 wide
    int wordCount = Editor::getInstance().wordCountFile + Editor::getInstance().wordCountBuffer;

    String right = savedTok + " | " + formatNumber(wordCount) + " words | " + locLabel;

    // While a background sync (Ctrl+U) is running, the right group shows its
    // progress instead; a finished/failed sync lingers ~3s then reverts.
    static unsigned int sync_clear_at = 0;
    int sync_state = app["sync_state"].as<int>();
    if (sync_state == SYNC_STARTED || sync_state == SYNC_PROGRESS)
    {
        sync_clear_at = 0; // still running
        String msg = app["sync_message"].as<String>();
        right = msg.isEmpty() ? String("syncing...") : capUtf8(msg, 38);
    }
    else if (sync_state == SYNC_COMPLETED || sync_state == SYNC_ERROR)
    {
        if (sync_clear_at == 0)
            sync_clear_at = millis() + 3000; // just settled; hold the result
        if (millis() < sync_clear_at)
        {
            if (sync_state == SYNC_COMPLETED)
            {
                String msg = app["sync_message"].as<String>();
                msg.trim();
                right = msg.isEmpty() ? String("synced") : capUtf8(msg, 38);
            }
            else // SYNC_ERROR — surface the actual reason (WiFi / URL / …)
            {
                String err = app["sync_error"].as<String>();
                err.trim();
                right = err.isEmpty() ? String("sync failed") : capUtf8(err, 38);
            }
        }
    }

    u8->setCursor(395 - u8->getUTF8Width(right.c_str()), STATUSBAR_Y);
    u8->print(right.c_str());
}

// Full-screen modal shown at boot when unsaved text was recovered.
void WP_render_recovery(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    display->clearDisplay();
    u8->setForegroundColor(ST7305_COLOR_BLACK);
    u8->setBackgroundColor(ST7305_COLOR_WHITE);

    display->drawLine(0, 95, screen_width, 95, 1);
    display->drawLine(0, 225, screen_width, 225, 1);

    u8->setFont(u8g2_font_profont22_mf);
    u8->setCursor(40, 130);
    u8->printf("RECOVERED TEXT");

    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(40, 165);
    u8->printf("Last session ended without saving.");
    u8->setCursor(40, 190);
    u8->printf("Unsaved text was found.");

    u8->setCursor(40, 255);
    u8->printf("Enter = Restore     Esc = Discard");
}

//
// Render Cursor
void WP_render_cursor(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    JsonDocument &app = status();

    // Cursor information
    static int cursorLinePos_prev = 0;
    int cursorLinePos = Editor::getInstance().cursorLinePos;
    int cursorLine = Editor::getInstance().cursorLine;
    int cursorPos = Editor::getInstance().cursorPos;

    // Calculate Cursor X position from the visual layout of the edit line, so
    // the caret lands under the right cell for both LTR and RTL text.
    static bidi::Cell cells[80];
    static int wpx[80];
    bool rtl = false;
    int n = WP_layout(cursorLine, cells, 80, &rtl);

    int total = 0;
    for (int c = 0; c < n; c++)
    {
        wpx[c] = WP_cell_width(u8, cells[c]);
        total += wpx[c];
    }
    // caret line is never justified (see WP_render_line), so the non-justify
    // alignment matches where its glyphs are actually drawn
    int x0 = WP_align_x(wp_align == ALIGN_JUSTIFY ? ALIGN_AUTO : wp_align, rtl, total);

    // byte offset of the cursor within its line
    char *lineStart = Editor::getInstance().linePositions[cursorLine];
    int cursorByteInLine = cursorPos - (int)(lineStart - Editor::getInstance().linePositions[0]);

    int cursorX;
    int v = -1;
    for (int c = 0; c < n; c++)
        if (cells[c].byteStart == cursorByteInLine)
        {
            v = c;
            break;
        }
    if (v >= 0)
    {
        cursorX = x0;                           // sum widths of cells before v
        for (int c = 0; c < v; c++)
            cursorX += wpx[c];
    }
    else                                        // end of the line's logical text
        cursorX = rtl ? x0 : (x0 + total);

    // Blink the cursor every 500 ms
    static bool blink = false;
    static unsigned int last = millis();
    if (millis() - last > 500)
    {
        last = millis();
        blink = !blink;
    }

    // The caret bar sits just under the active line's baseline, which moves with
    // the scroll mode (caretBaselineY, set by WP_render_text). The bottom line
    // keeps the legacy offset (lands in the status-bar gap); the other modes use
    // a smaller offset so the bar hugs its own line instead of grazing the line
    // below it.
    int barY = caretBaselineY + (caretBaselineY == editY ? (cursorY - editY) : 3);

    // Delete previous cursor line
    if (cursorLinePos != cursorLinePos_prev)
    {
        //
        display->drawFilledRectangle(
            0,
            barY,
            screen_width,
            barY + cursorHeight,
            0);

        //
        cursorLinePos_prev = cursorLinePos;
    }

    // Cursor Blink sits under the active line (bottom edge in bottom mode)
    if (blink)
        display->drawFilledRectangle(cursorX, barY, cursorX + font_width, barY + cursorHeight, 1);
    else
        display->drawFilledRectangle(cursorX, barY, cursorX + font_width, barY + cursorHeight, 0);
}

//
void WP_keyboard(int key, bool pressed, int index)
{
    // Every key stroke resets sleep timer
    last_sleep = millis();

    //
    JsonDocument &app = status();

    // Crash-recovery modal owns the keyboard until answered. Enter restores the
    // recovered text, Esc/Menu discards it (keeping the last clean save). Both
    // keys are layout-independent, so this works under any keyboard layout.
    if (recovery_prompt)
    {
        if (!pressed)
            return;
        if (key == '\n' || key == '\r')
            Editor::getInstance().applyRecovery();
        else if (key == 27 || key == MENU)
            Editor::getInstance().clearRecovery();
        else
            return; // ignore anything else; stay on the prompt
        recovery_prompt = false;
        clear_background = true;
        return;
    }

    // Editor shortcut overlay (Ctrl+/) owns the keyboard: any key dismisses it
    // and drops straight back into the open file. Close on key-UP and swallow
    // both edges, so the dismiss key isn't reprocessed (e.g. Esc would otherwise
    // close the overlay AND open the menu) or typed into the document.
    if (help_overlay)
    {
        if (!pressed)
        {
            help_overlay = false;
            // pageChanged forces a full clearDisplay + redraw so the overlay is
            // fully wiped (clear_background alone doesn't clear the panel).
            Editor::getInstance().pageChanged = true;
        }
        return;
    }

    // Manual save (Ctrl+S / Fn+S)
    if (key == SAVE)
    {
        Editor::getInstance().saveFile();
        saved_flash_until = millis() + 1500; // flash "saved" (only on manual save)
        return;
    }

    // Ctrl+U: sync ONLY the open file. Push local edits and, if Drive's copy is
    // newer, pull it and refresh the editor (a true both-sides edit keeps both).
    // Flush the buffer first so the latest content is what syncs.
    if (key == SYNC)
    {
        Editor::getInstance().saveFile();
        app["sync_scope"] = "one";
        app["sync_one"] = app["config"]["file_index"].as<int>();
        app["sync_reload"] = -1;
        sync_init();
        sync_start_request();
        return;
    }

    // Ctrl+Shift+U: full two-way sync of ALL files. Background; progress shows in
    // the status bar; the open file's content-pull is deferred so typing is safe.
    if (key == SYNC_ALL)
    {
        Editor::getInstance().saveFile();
        app["sync_scope"] = "all";
        sync_init();
        sync_start_request();
        return;
    }

    // Ctrl+/ : show the editor shortcut overlay (any key returns to the file).
    if (key == HELP_KEY)
    {
        help_overlay = true;
        help_painted = false;
        clear_background = true; // force WP_render to run and paint the overlay
        return;
    }

    // Ctrl+, (Preferences) is handled directly in keyboard.cpp so the shortcut
    // works from the editor and any menu screen alike.

    // Insert the date+time at the caret (Ctrl+D). clock_datestr() is
    // "YYYY-MM-DD HH:MM" when the clock is live, "YYYY-MM-DD" when only a cached
    // date is known (no live time), and "----------" when never set - only
    // insert when it starts with a digit, so an unknown date inserts nothing
    // instead of dashes. Fed through the normal typing path (press+release per
    // char) so it flips saved and is undoable, exactly like typing it by hand.
    if (key == DATE_INSERT)
    {
        String d = clock_datestr();
        if (d.length() >= 10 && d[0] >= '0' && d[0] <= '9')
        {
            for (int i = 0; i < (int)d.length(); i++)
            {
                Editor::getInstance().keyboard(d[i], true);
                Editor::getInstance().keyboard(d[i], false);
            }
        }
        return;
    }

    // Toggle the bottom status bar (Ctrl+H / Fn+H)
    if (key == STATUSBAR)
    {
        statusbar_hidden = !statusbar_hidden;
        applyStatusbarLayout(); // reclaim/release the bar's band for text
        Editor::getInstance().pageChanged = true; // full clear+redraw for the new layout
        return;
    }

    // Check if menu key is pressed
    if (key == MENU || key == 27) // or ESC key
    {
        if (!pressed)
        {
            // Save before transitioning to the menu
            Editor::getInstance().saveFile();

            // leaving the editor clears any active selection
            Editor::getInstance().clearSelection();
            clear_background = true;

            //
            app["screen"] = MENUSCREEN;

            //
            _debug("WP_keyboard::Moving to Menu Screen\n");
        }
    }

    // Check if File Change request is pressed
    else if (key >= 1000 && key <= 1010)
    {
        if (!pressed)
        {
            int fileIndex = key - 1000;
            _log("File Change Requested: %d\n", fileIndex);

            //
            Editor::getInstance().saveFile();

            // save config
            app["config"]["file_index"] = fileIndex;
            config_save();

            // load new file
            Editor::getInstance().loadFile(format("/%d.txt", fileIndex));
        }
    }

    else
    {
        // Left/Right are visual: in an RTL line, physical Left should move the
        // caret visually left, which is logically *forward*. The editor moves
        // in logical order (18=back, 19=forward), so swap them when the current
        // line renders right-to-left. (Up/Down/Home/End stay as-is.) The same
        // visual swap applies to the Shift+arrow selection-extend codes, the
        // Ctrl+arrow word-jump codes and the Ctrl+Shift+arrow word-extend codes,
        // so the caret (and any selection) moves in the direction the user sees.
        if (key == 18 || key == 19 ||
            key == WORD_LEFT || key == WORD_RIGHT ||
            key == SEL_LEFT || key == SEL_RIGHT ||
            key == SEL_WORD_LEFT || key == SEL_WORD_RIGHT)
        {
            static bidi::Cell rtlCells[80];
            bool rtl = false;
            WP_layout(Editor::getInstance().cursorLine, rtlCells, 80, &rtl);
            if (rtl)
            {
                if (key == 18) key = 19;
                else if (key == 19) key = 18;
                else if (key == WORD_LEFT) key = WORD_RIGHT;
                else if (key == WORD_RIGHT) key = WORD_LEFT;
                else if (key == SEL_LEFT) key = SEL_RIGHT;
                else if (key == SEL_RIGHT) key = SEL_LEFT;
                else if (key == SEL_WORD_LEFT) key = SEL_WORD_RIGHT;
                else if (key == SEL_WORD_RIGHT) key = SEL_WORD_LEFT;
            }
        }

        // send the keys to the editor (selection-extend, clipboard actions and
        // plain nav are all interpreted inside Editor::keyboard)
        Editor::getInstance().keyboard(key, pressed);
    }
}
