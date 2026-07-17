#include "Clear.h"
#include "../Menu.h"
#include "app/app.h"
#include "display/display.h"
#include "service/Editor/Editor.h"
#include "service/Tools/TextUtil.h"
#include "display/RLCD/display_RLCD.h"

//
void Clear_setup(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{
    // when entering the screen
    // clear the screen
    Menu_clear();
}

//
void Clear_render(ST7305_4p2_BW_DisplayDriver *display, U8G2_FOR_ST73XX *u8)
{    
    JsonDocument &app = status();
    int fi = app["config"]["file_index"].as<int>();
    String title = app["config"][format("title_%d", fi)].as<String>();

    Menu_drawHeader(display, u8, "DELETE FILE");

    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(10, 62);
    u8->print("Delete this file?");

    // The target file, emphasised in an inverse bar (same look as the list
    // highlight) so it's unmistakable which file is about to go. Number first
    // (LTR), then the name (shaped, so Arabic renders).
    display->drawFilledRectangle(8, 80, 392, 102, 1);
    u8->setForegroundColor(ST7305_COLOR_WHITE);
    u8->setBackgroundColor(ST7305_COLOR_BLACK);
    u8->setCursor(16, 97);
    u8->printf("[%d]  ", fi);
    if (title.isEmpty() || title == "null")
        u8->print("(empty)");
    else
        RLCD_drawShapedLabel(u8, u8->getCursorX(), 97, capUtf8(title, 22).c_str(), false);
    u8->setForegroundColor(ST7305_COLOR_BLACK);
    u8->setBackgroundColor(ST7305_COLOR_WHITE);

    // plain-language note: deletion isn't reversible on the device
    u8->setFont(u8g2_font_profont17_tf);
    u8->setCursor(10, 150);
    u8->print("This can't be undone on the device.");
    u8->setCursor(10, 172);
    u8->print("Sync first to keep a safe copy.");

    display->drawLine(0, 276, 400, 276, 1);
    u8->setCursor(10, 296);
    u8->print("[Y] delete      [B] back");
}

//
void Clear_keyboard(char key)
{
    JsonDocument &app = status();

    // delete confirmed
    if (key == 'Y' || key == 'y')
    {
        // If this file is synced, leave a tombstone so the next sync trashes the
        // remote copy too, and clear the slot's sync mapping so a reused slot
        // can't cross-link to the old remote file. Tombstone both backends by
        // whatever mapping exists, so a delete still propagates after switching
        // providers.
        int delIdx = app["config"]["file_index"].as<int>();
        String did = app["config"][format("drive_id_%d", delIdx)].as<String>();
        if (!did.isEmpty() && did != "null")
        {
            JsonArray tr = app["config"]["sync_trash"].as<JsonArray>();
            if (tr.isNull())
                tr = app["config"]["sync_trash"].to<JsonArray>();
            tr.add(did);
        }
        String gp = app["config"][format("gh_path_%d", delIdx)].as<String>();
        if (!gp.isEmpty() && gp != "null")
        {
            JsonArray trg = app["config"]["sync_trash_git"].as<JsonArray>();
            if (trg.isNull())
                trg = app["config"]["sync_trash_git"].to<JsonArray>();
            trg.add(gp);
        }
        app["config"].remove(format("drive_id_%d", delIdx));
        app["config"].remove(format("gh_path_%d", delIdx));
        app["config"].remove(format("gh_sha_%d", delIdx));
        app["config"].remove(format("synced_modified_%d", delIdx));
        app["config"].remove(format("synced_hash_%d", delIdx));
        app["config"].remove(format("synced_title_%d", delIdx));

        // remove the file from the list (keeps a backup)
        Editor::getInstance().deleteFile();

        // repoint the current file to a surviving slot, else the just-deleted
        // index would be recreated empty the next time the editor opens it
        int next = 0;
        for (int i = 0; i < 100; i++)
            if (gfs()->exists(format("/%d.txt", i).c_str())) { next = i; break; }
        app["config"]["file_index"] = next;
        config_save();
        Editor::getInstance().loadFile(format("/%d.txt", next));
    }

    // Return to the file list either way (confirmed or cancelled) - staying in
    // the menu, not dropping into the editor. Re-entering MENU_HOME re-runs
    // Home_setup, so the list reflects the deletion.
    app["menu"]["state"] = MENU_HOME;
}
