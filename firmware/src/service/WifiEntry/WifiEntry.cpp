#include "WifiEntry.h"
#include "app/app.h"
#include "display/display.h"

// services
#include "service/Buffer/BufferService.h"
#include "display/RLCD/Menu/FileList/Pagination.h"

//
int wifi_config_status = 0;
int wifi_config_index = 0;

void WifiEntry_setup()
{
    //
    buffer_clear();

    // load wifi configuration from SPIFF
    wifi_config_load();

    // set the screen to show the wifi configuration list
    wifi_config_status = WIFI_CONFIG_LIST;
    status()["wifi_cursor"] = 0;
}

//
void WifiEntry_keyboard(char key)
{
    // non printable keys are not going to be going through the buffer
    if(key == 0) return;

    //
    _debug("WifiEntry_keyboard key: [%d] %c, wifi_config_status: %d\n", key, key, wifi_config_status);

    //
    JsonDocument &app = status();

    // chosen-entry screen: edit or forget this saved network
    if (wifi_config_status == WIFI_CONFIG_ENTRY)
    {
        if (key == 27 || key == MENU || key == 'B' || key == 'b')
        {
            wifi_config_status = WIFI_CONFIG_LIST;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }
        if (key == '\n' || key == 'E' || key == 'e')
        {
            buffer_clear();
            wifi_config_status = WIFI_CONFIG_EDIT_SSID;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }
        if (key == 'F' || key == 'f')
        {
            // forget: drop the entry and persist
            JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
            if ((int)savedAccessPoints.size() > wifi_config_index)
                savedAccessPoints.remove(wifi_config_index);
            wifi_config_save();
            wifi_config_status = WIFI_CONFIG_LIST;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }
        return;
    }

    // scan & pick a nearby network
    if (wifi_config_status == WIFI_CONFIG_SCAN)
    {
        if (app["wifi_scanning"] | false)
            return; // scan running; ignore input until it finishes

        JsonArray scan = app["network"]["scan"].as<JsonArray>();
        int count = (int)scan.size();
        int cursor = paginate::clampInt(app["wifi_cursor"] | 0, 0, count > 0 ? count - 1 : 0);

        if (key == 27 || key == MENU || key == 'B' || key == 'b')
        {
            wifi_config_status = WIFI_CONFIG_LIST;
            app["wifi_config_status"] = wifi_config_status;
            app["wifi_cursor"] = 0;
            return;
        }
        if (key == 'R' || key == 'r' || key == 'S' || key == 's')
        {
            app["wifi_scanning"] = true; // rescan
            app["wifi_cursor"] = 0;
            return;
        }
        if (count == 0)
            return;

        if (key == 20) { app["wifi_cursor"] = paginate::clampInt(cursor - 1, 0, count - 1); return; }
        if (key == 21) { app["wifi_cursor"] = paginate::clampInt(cursor + 1, 0, count - 1); return; }
        if (key == 22) { app["wifi_cursor"] = paginate::clampInt(cursor - WIFI_PER_PAGE, 0, count - 1); return; }
        if (key == 23) { app["wifi_cursor"] = paginate::clampInt(cursor + WIFI_PER_PAGE, 0, count - 1); return; }

        int pick = -1;
        if (key == '\n' || key == '\r')
            pick = cursor;
        else if (key >= '1' && key <= '9')
        {
            int page = paginate::pageOf(cursor, WIFI_PER_PAGE);
            int rows = paginate::rowsOnPage(page, WIFI_PER_PAGE, count);
            int row = key - '1';
            if (row < rows)
                pick = page * WIFI_PER_PAGE + row;
        }
        if (pick >= 0 && pick < count)
        {
            // prefill the chosen SSID, then drop into the normal add flow
            // (confirm SSID with Enter -> type password -> save) at a new slot.
            String ssid = scan[pick]["ssid"].as<String>();
            buffer_clear();
            for (int i = 0; i < (int)ssid.length(); i++)
                buffer_add(ssid[i]);

            JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
            wifi_config_index = (int)aps.size();
            app["wifi_config_index"] = wifi_config_index;
            wifi_config_status = WIFI_CONFIG_EDIT_SSID;
            app["wifi_config_status"] = wifi_config_status;
        }
        return;
    }

    //
    if (wifi_config_status >= WIFI_CONFIG_EDIT_SSID)
    {
        // ESC cancels editing and returns to the saved-network list
        if (key == 27 || key == MENU)
        {
            buffer_clear();
            wifi_config_status = WIFI_CONFIG_LIST;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }
        // SAVE or NEXT
        if (key == '\n')
        {
            // NEXT step
            if (wifi_config_status == WIFI_CONFIG_EDIT_SSID)
            {
                // save ssid
                JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
                savedAccessPoints[wifi_config_index]["ssid"] = String(buffer_get());

                // clear buffer
                buffer_clear();

                // move to password enter screen
                wifi_config_status = WIFI_CONFIG_EDIT_KEY;
                app["wifi_config_status"] = wifi_config_status;
            }
            else if (wifi_config_status == WIFI_CONFIG_EDIT_KEY)
            {
                // save ssid
                JsonArray savedAccessPoints = app["wifi"]["access_points"].as<JsonArray>();
                savedAccessPoints[wifi_config_index]["password"] = String(buffer_get());

                // save the configuration
                wifi_config_save();

                //
                buffer_clear();

                // go back to the configuration list
                wifi_config_status = WIFI_CONFIG_LIST;
                app["wifi_config_status"] = wifi_config_status;
            }
        }
        // BACK SPACE
        else if (key == '\b')
        {
            // backspace
            buffer_remove();
        }
        // ADD KEYS
        else
        {
            // edit mode
            buffer_add(key);

            //
            _debug("WifiEntry_keyboard buffer_add: %c, %s\n", key, buffer_get());
        }
    }
    else
    {
        // ---- saved-network list (unbounded, cursor-driven) ----
        JsonArray aps = app["wifi"]["access_points"].as<JsonArray>();
        int count = (int)aps.size();
        int cursor = paginate::clampInt(app["wifi_cursor"] | 0, 0, count > 0 ? count - 1 : 0);

        // back to the tab this screen was opened from (Settings or Files)
        if (key == 27 || key == MENU || key == 'B' || key == 'b')
        {
            app["menu"]["state"] = app["menu"]["return"] | MENU_SETTINGS;
            return;
        }

        // S: scan for nearby networks and pick one
        if (key == 'S' || key == 's')
        {
            app["wifi_scanning"] = true;
            app["wifi_cursor"] = 0;
            wifi_config_status = WIFI_CONFIG_SCAN;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }

        // N: add a new network — edit a fresh slot at the end (created on save,
        // so a cancelled "New" leaves nothing behind).
        if (key == 'N' || key == 'n')
        {
            buffer_clear();
            wifi_config_index = count;
            app["wifi_config_index"] = wifi_config_index;
            wifi_config_status = WIFI_CONFIG_EDIT_SSID;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }

        if (count == 0)
            return; // nothing to navigate yet

        if (key == 20) { app["wifi_cursor"] = paginate::clampInt(cursor - 1, 0, count - 1); return; }              // Up
        if (key == 21) { app["wifi_cursor"] = paginate::clampInt(cursor + 1, 0, count - 1); return; }              // Down
        if (key == 22) { app["wifi_cursor"] = paginate::clampInt(cursor - WIFI_PER_PAGE, 0, count - 1); return; }  // Page Up
        if (key == 23) { app["wifi_cursor"] = paginate::clampInt(cursor + WIFI_PER_PAGE, 0, count - 1); return; }  // Page Down

        // Enter: open the focused entry (edit / forget)
        if (key == '\n' || key == '\r')
        {
            wifi_config_index = cursor;
            app["wifi_config_index"] = wifi_config_index;
            wifi_config_status = WIFI_CONFIG_ENTRY;
            app["wifi_config_status"] = wifi_config_status;
            return;
        }

        // digit: quick-select a visible row on the current page (page-relative)
        if (key >= '1' && key <= '9')
        {
            int page = paginate::pageOf(cursor, WIFI_PER_PAGE);
            int rows = paginate::rowsOnPage(page, WIFI_PER_PAGE, count);
            int row = key - '1';
            if (row < rows)
            {
                wifi_config_index = page * WIFI_PER_PAGE + row;
                app["wifi_config_index"] = wifi_config_index;
                wifi_config_status = WIFI_CONFIG_ENTRY;
                app["wifi_config_status"] = wifi_config_status;
            }
            return;
        }
    }
}

// For safety concerns saving wifi information sd card can easily expose security information
// Wifi Configuration is saved internal storage of ESP32
void wifi_config_load()
{
    //
    JsonDocument &app = status();

    // load config.json
    _log("Opening wifi.json file from internal storage\n");
    File file = gfs()->open("/wifi.json", "r");
    if (file)
    {
        // read the file
        _log("Reading wifi.json file\n");
        String wifiString = file.readString();
        _log("Closing wifi.json file\n");
        file.close();
        delay(100);

        // check if configString is empty
        if (wifiString.isEmpty())
        {
            // to avoid deserialization failure whem empty
            wifiString = "{}";
        }

        // Prepare a JsonDocument for the configuration
        // The size should be adjusted according to your configuration's needs
        JsonDocument configDoc;

        // convert to JsonObject
        DeserializationError error = deserializeJson(configDoc, wifiString);
        _log("Deserializing wifi.json file\n");
        if (error)
        {
            //
            _log("wifi.json deserializeJson() failed: %s\n", error.c_str());

            //
            app["error"] = "Wrong format wifi.json";
            app["screen"] = ERRORSCREEN;

            // delete wifi.json from SPIFF
            gfs()->remove("/wifi.json");

            return;
        }

        // Assign the loaded configuration to "config" property of app
        _log("Loading wifi config\n");
        app["wifi"] = configDoc.as<JsonObject>();

        // print out the configuration
        _log("Wifi config loaded successfully!\n");
    }
    else
    {
        // file doesn't exist
        _log("wifi.json file doens't exist\n");

        return;
    }
}

// save current wifi configuration
void wifi_config_save()
{
    // load app status
    JsonDocument &app = status();

    // save config
    // Open the file for writing
    File file = gfs()->open("/wifi.json", FILE_WRITE);
    if (!file)
    {
        _log("Failed to open wifi.json file for writing.\n");
        return;
    }

    // Serialize the "config" property of the app Document directly to the file
    if (app["wifi"].is<JsonObject>())
    {
        String jsonOutput;
        serializeJsonPretty(app["wifi"], jsonOutput);
        file.println(jsonOutput);

        // debug
        _debug("wifi_config_save\n%s\n", jsonOutput.c_str());

        //
        _log("Wifi config updated successfully.\n");
    }
    else
    {
        _log("No 'wifi' property found in app Document.\n");
    }

    // close config.json
    file.close();
    delay(100);
}
