#include "BLEHost.h"

// Marking the menu dirty forces one repaint when async BLE state changes
// (scan finished, connected, disconnected). Declared in the RLCD Menu module.
void Menu_clear();

#ifdef USE_BLE_KEYBOARD_HOST

#include <Arduino.h>
#include <vector>
#include <string>
#include <NimBLEDevice.h>

#include "app/app.h"
#include "keyboard/keyboard.h"

static const NimBLEUUID UUID_HID_SERVICE((uint16_t)0x1812);
static const uint16_t APPEARANCE_KEYBOARD = 0x03C1;
static const uint32_t DISCOVER_MS = 6000;

enum Mode
{
    MODE_IDLE,
    MODE_RECONNECT, // scanning for the remembered keyboard, auto-connect on sight
    MODE_DISCOVER   // timed scan populating the pick list
};

static Mode g_mode = MODE_IDLE;
static NimBLEClient *g_client = nullptr;
static NimBLEAddress g_target;      // address we're about to connect to
static NimBLEAddress g_remembered;  // paired keyboard, null if none
static volatile bool g_wantConnect = false;
static volatile bool g_scanDone = false;
static volatile bool g_connected = false;
static uint8_t g_prevKeys[6] = {0};
static std::vector<NimBLEAddress> g_listAddrs; // parallel to app["ble"]["devices"]

static void setStatus(const char *s)
{
    status()["ble"]["status"] = s;
    Menu_clear();
}

static bool inReport(const uint8_t *keys, uint8_t code)
{
    for (int i = 0; i < 6; i++)
        if (keys[i] == code)
            return true;
    return false;
}

// Boot-layout keyboard report: [0]=modifier, [1]=reserved, [2..7]=six usage
// codes. Diff against the previous report and emit press/release edges through
// the shared HID pipeline (locale, Arabic, shortcuts all handled there).
static void onNotify(NimBLERemoteCharacteristic *chr, uint8_t *data, size_t len, bool isNotify)
{
    if (len < 8)
        return; // consumer/media/vendor report - ignore for now

    uint8_t mod = data[0];
    const uint8_t *keys = data + 2;

    for (int i = 0; i < 6; i++)
    {
        uint8_t pk = g_prevKeys[i];
        if (pk >= 0x04 && !inReport(keys, pk))
            keyboard_HID2Ascii(pk, mod, false);
    }
    for (int i = 0; i < 6; i++)
    {
        uint8_t nk = keys[i];
        if (nk >= 0x04 && !inReport(g_prevKeys, nk))
            keyboard_HID2Ascii(nk, mod, true);
    }
    memcpy(g_prevKeys, keys, 6);
}

class HostScanCallbacks : public NimBLEScanCallbacks
{
    void onResult(const NimBLEAdvertisedDevice *dev) override
    {
        if (g_mode == MODE_RECONNECT && !g_remembered.isNull() &&
            dev->getAddress() == g_remembered)
        {
            g_target = g_remembered;
            g_wantConnect = true;
            NimBLEDevice::getScan()->stop();
        }
        // discovery results are read from getResults() in the main loop
    }
    void onScanEnd(const NimBLEScanResults &, int) override
    {
        if (g_mode == MODE_DISCOVER)
            g_scanDone = true;
    }
};

class HostClientCallbacks : public NimBLEClientCallbacks
{
    void onDisconnect(NimBLEClient *c, int reason) override
    {
        _log("[blehost] disconnected (reason %d)\n", reason);
        g_connected = false;
        g_client = nullptr;
        memset(g_prevKeys, 0, sizeof(g_prevKeys));
        status()["ble"]["connected"] = false;
        if (!g_remembered.isNull())
            g_mode = MODE_RECONNECT;
        setStatus("Disconnected, retrying...");
    }
    void onAuthenticationComplete(NimBLEConnInfo &info) override
    {
        _log("[blehost] auth: bonded=%s encrypted=%s\n",
             info.isBonded() ? "yes" : "no", info.isEncrypted() ? "yes" : "no");
    }
};

static HostScanCallbacks g_scanCB;
static HostClientCallbacks g_clientCB;

static void startReconnectScan()
{
    NimBLEScan *s = NimBLEDevice::getScan();
    if (s->isScanning())
        return;
    s->setScanCallbacks(&g_scanCB, false);
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(80);
    s->start(0, false, true); // scan until the remembered keyboard appears
}

void blehost_scan_start()
{
    NimBLEScan *s = NimBLEDevice::getScan();
    s->stop();
    s->clearResults();
    g_listAddrs.clear();
    g_mode = MODE_DISCOVER;

    JsonDocument &app = status();
    app["ble"]["devices"].to<JsonArray>();
    app["ble"]["scanning"] = true;
    setStatus("Scanning...");

    s->setScanCallbacks(&g_scanCB, false);
    s->setActiveScan(true);
    s->setInterval(100);
    s->setWindow(80);
    s->start(DISCOVER_MS, false, true);
}

static void buildDeviceList()
{
    JsonDocument &app = status();
    JsonArray arr = app["ble"]["devices"].to<JsonArray>();
    g_listAddrs.clear();

    NimBLEScanResults res = NimBLEDevice::getScan()->getResults();
    for (int i = 0; i < res.getCount(); i++)
    {
        const NimBLEAdvertisedDevice *d = res.getDevice(i);
        bool kb = d->isAdvertisingService(UUID_HID_SERVICE) ||
                  d->getAppearance() == APPEARANCE_KEYBOARD;
        if (!kb)
            continue;

        NimBLEAddress a = d->getAddress();
        std::string nm = d->getName();
        JsonObject o = arr.add<JsonObject>();
        o["name"] = nm.empty() ? String(a.toString().c_str()) : String(nm.c_str());
        o["addr"] = String(a.toString().c_str());
        g_listAddrs.push_back(a);
    }

    app["ble"]["scanning"] = false;
    g_mode = MODE_IDLE;
    setStatus(arr.size() ? "Select a keyboard" : "No keyboards found");
}

static void connectToKeyboard()
{
    NimBLEScan *s = NimBLEDevice::getScan();
    if (s->isScanning())
        s->stop();

    NimBLEClient *c = NimBLEDevice::getClientByPeerAddress(g_target);
    if (!c)
        c = NimBLEDevice::createClient(g_target);
    c->setClientCallbacks(&g_clientCB, false);

    if (!c->connect(g_target))
    {
        _log("[blehost] connect failed\n");
        NimBLEDevice::deleteClient(c);
        setStatus("Connect failed");
        if (!g_remembered.isNull())
        {
            g_mode = MODE_RECONNECT;
            startReconnectScan();
        }
        return;
    }

    c->secureConnection(); // bond/encrypt before touching the HID service

    NimBLERemoteService *hid = c->getService(UUID_HID_SERVICE);
    if (!hid)
    {
        setStatus("No HID service");
        c->disconnect();
        return;
    }

    int subs = 0;
    for (auto *ch : hid->getCharacteristics(true))
        if (ch->canNotify() && ch->subscribe(true, onNotify))
            subs++;

    if (subs == 0)
    {
        setStatus("No key reports");
        c->disconnect();
        return;
    }

    g_client = c;
    g_connected = true;
    memset(g_prevKeys, 0, sizeof(g_prevKeys));
    status()["ble"]["connected"] = true;
    _log("[blehost] connected, %d report(s)\n", subs);
    setStatus("Connected");
}

void blehost_connect_index(int index)
{
    JsonDocument &app = status();
    JsonArray arr = app["ble"]["devices"].as<JsonArray>();
    if (index < 0 || index >= (int)arr.size() || index >= (int)g_listAddrs.size())
        return;

    g_target = g_listAddrs[index];
    g_remembered = g_target;

    String name = arr[index]["name"].as<String>();
    app["ble"]["peer"] = name;
    app["config"]["ble_addr"] = String(g_target.toString().c_str());
    app["config"]["ble_type"] = (int)g_target.getType();
    app["config"]["ble_name"] = name;
    config_save();

    g_mode = MODE_RECONNECT;
    g_wantConnect = true;
    setStatus("Connecting...");
}

void blehost_forget()
{
    JsonDocument &app = status();
    g_remembered = NimBLEAddress();
    app["config"].remove("ble_addr");
    app["config"].remove("ble_type");
    app["config"].remove("ble_name");
    config_save();

    if (g_client)
        g_client->disconnect();
    g_connected = false;
    app["ble"]["connected"] = false;
    app["ble"]["peer"] = "";
    g_mode = MODE_IDLE;
    setStatus("No keyboard paired");
}

bool blehost_is_scanning() { return status()["ble"]["scanning"] | false; }
bool blehost_is_connected() { return g_connected; }

void blehost_setup()
{
    NimBLEDevice::init("katibOS");
    NimBLEDevice::setPower(3 /* dBm */);
    NimBLEDevice::setSecurityAuth(true, false, true); // bond, no MITM, SC -> Just Works
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

    JsonDocument &app = status();
    app["ble"]["devices"].to<JsonArray>();
    app["ble"]["scanning"] = false;
    app["ble"]["connected"] = false;

    String addr = app["config"]["ble_addr"].as<String>();
    if (addr.length() > 0)
    {
        int type = app["config"]["ble_type"] | 0;
        g_remembered = NimBLEAddress(std::string(addr.c_str()), (uint8_t)type);
        String name = app["config"]["ble_name"].as<String>();
        app["ble"]["peer"] = name.isEmpty() ? addr : name;
        g_mode = MODE_RECONNECT;
        startReconnectScan();
        setStatus("Looking for keyboard...");
    }
    else
    {
        app["ble"]["peer"] = "";
        g_mode = MODE_IDLE;
        setStatus("No keyboard paired");
    }
}

void blehost_loop()
{
    if (g_wantConnect)
    {
        g_wantConnect = false;
        connectToKeyboard();
        return;
    }
    if (g_scanDone)
    {
        g_scanDone = false;
        buildDeviceList();
        return;
    }
    if (g_mode == MODE_RECONNECT && !g_connected)
    {
        NimBLEScan *s = NimBLEDevice::getScan();
        if (!s->isScanning())
            startReconnectScan();
    }
}

#else // USE_BLE_KEYBOARD_HOST not defined -> no-op stubs (emulator / other boards)

void blehost_setup() {}
void blehost_loop() {}
void blehost_scan_start() {}
bool blehost_is_scanning() { return false; }
void blehost_connect_index(int) {}
void blehost_forget() {}
bool blehost_is_connected() { return false; }

#endif // USE_BLE_KEYBOARD_HOST
