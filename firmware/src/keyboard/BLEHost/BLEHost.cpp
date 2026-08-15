#include "BLEHost.h"

// Marking the menu dirty forces one repaint when async BLE state changes
// (scan finished, connected, disconnected). Declared in the RLCD Menu module.
void Menu_clear();

#ifdef USE_BLE_KEYBOARD_HOST

#include <Arduino.h>
#include <vector>
#include <string>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "app/app.h"
#include "keyboard/keyboard.h"

static const NimBLEUUID UUID_HID_SERVICE((uint16_t)0x1812);
static const NimBLEUUID UUID_PROTOCOL_MODE((uint16_t)0x2A4E);   // 0=Boot, 1=Report
static const NimBLEUUID UUID_BOOT_KB_INPUT((uint16_t)0x2A22);   // 8-byte boot report
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
// The connect is a state machine driven by NimBLE's callbacks, not a straight
// line of blocking calls: connect(), secureConnection() and service discovery
// each wait on the peer, and a keyboard whose pairing key we no longer share
// can leave secureConnection() parked forever - which froze the whole device,
// since this all ran inline on the main loop task. Each step now returns
// immediately and the next one is picked up by blehost_loop().
static volatile bool g_wantSecure = false;   // link is up, ask for encryption
static volatile bool g_wantSetup = false;    // encrypted, discover + subscribe
static volatile bool g_connecting = false;   // an attempt is in flight
static unsigned long g_connectStart = 0;
static const unsigned long CONNECT_LIMIT_MS = 12000;
static volatile bool g_scanDone = false;
static volatile bool g_connected = false;
static uint8_t g_prevKeys[6] = {0};
static uint8_t g_prevMod = 0;
static std::vector<NimBLEAddress> g_listAddrs; // parallel to app["ble"]["devices"]

// onNotify() runs on the NimBLE host task; the editor/display it feeds are
// owned by the main loop task. Hand key edges across via a queue and apply them
// in blehost_loop() so editing never happens from the BLE callback context.
struct BleKeyEvent { uint8_t code; uint8_t mod; bool pressed; };
static QueueHandle_t g_keyQueue = nullptr;

static void enqueueKey(uint8_t code, uint8_t mod, bool pressed)
{
    if (!g_keyQueue)
        return;
    BleKeyEvent ev{code, mod, pressed};
    xQueueSend(g_keyQueue, &ev, 0); // drop if full rather than block the BLE task
}

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
    // Report shapes differ per keyboard - a NuPhy notifies on 0x2A4D even after
    // being told to use boot protocol - so say what actually arrived. Empty
    // frames are skipped (an idle keyboard streams them ~10/s and they would
    // flush the cap before a real keystroke appeared) and the rest is capped:
    // this runs on the BLE task, once per report.
    static int seen = 0;
    bool empty = true;
    for (size_t i = 0; i < len && empty; i++)
        if (data[i])
            empty = false;
    if (!empty && seen < 24)
    {
        seen++;
        char hex[3 * 12 + 1];
        size_t n = len < 12 ? len : 12;
        for (size_t i = 0; i < n; i++)
            snprintf(hex + i * 3, 4, "%02X ", data[i]);
        hex[n * 3] = 0;
        _log("[blehost] notify %s len=%u %s\n", chr->getUUID().toString().c_str(),
             (unsigned)len, hex);
    }

    if (len < 8)
        return; // consumer/media/vendor report - ignore for now

    uint8_t mod = data[0];
    const uint8_t *keys = data + 2;

    for (int i = 0; i < 6; i++)
    {
        uint8_t pk = g_prevKeys[i];
        if (pk >= 0x04 && !inReport(keys, pk))
            enqueueKey(pk, mod, false);
    }
    for (int i = 0; i < 6; i++)
    {
        uint8_t nk = keys[i];
        if (nk >= 0x04 && !inReport(g_prevKeys, nk))
            enqueueKey(nk, mod, true);
    }
    memcpy(g_prevKeys, keys, 6);

    // Both Cmd keys together (left-GUI + right-GUI) = back/menu, an easy thumb
    // chord for Mac keyboards that lack an Esc. Fire on the chord's edges by
    // injecting a synthetic Esc (0x29), reusing the Esc->MENU mapping.
    const uint8_t BOTH_GUI = 0x88; // LEFTGUI(0x08) | RIGHTGUI(0x80)
    bool nowBoth = (mod & BOTH_GUI) == BOTH_GUI;
    bool wasBoth = (g_prevMod & BOTH_GUI) == BOTH_GUI;
    if (nowBoth && !wasBoth)
        enqueueKey(0x29, mod, true);
    else if (!nowBoth && wasBoth)
        enqueueKey(0x29, mod, false);
    g_prevMod = mod;
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
    void onConnect(NimBLEClient *c) override
    {
        _log("[blehost] link up, requesting encryption\n");
        g_wantSecure = true;
    }
    void onConnectFail(NimBLEClient *c, int reason) override
    {
        _log("[blehost] connect failed (reason %d)\n", reason);
        g_connecting = false;
        setStatus("Connect failed");
        if (!g_remembered.isNull())
            g_mode = MODE_RECONNECT;
    }
    void onDisconnect(NimBLEClient *c, int reason) override
    {
        _log("[blehost] disconnected (reason %d) peer %s\n", reason,
             c ? c->getPeerAddress().toString().c_str() : "?");
        g_connected = false;
        g_connecting = false;
        g_wantSecure = g_wantSetup = false;
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
        if (info.isEncrypted())
            g_wantSetup = true;
        else
            g_connecting = false; // let the loop's deadline clean this up
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

// Every step below can block for seconds, all of it on the main loop task, and
// this board loses its panic text with the USB-CDC console that dies alongside
// it - so the trace has to say which step it was stuck in when it went.
#define BLE_STEP(fmt, ...) _log("[blehost] +%lums " fmt "\n", millis() - t0, ##__VA_ARGS__)

static void connectToKeyboard()
{
    unsigned long t0 = millis();
    BLE_STEP("connect to %s", g_target.toString().c_str());

    NimBLEScan *s = NimBLEDevice::getScan();
    if (s->isScanning())
        s->stop();
    BLE_STEP("scan stopped");

    NimBLEClient *c = NimBLEDevice::getClientByPeerAddress(g_target);
    if (!c)
        c = NimBLEDevice::createClient(g_target);
    c->setClientCallbacks(&g_clientCB, false);

    // HID keyboards (e.g. Logitech Keys-To-Go) drop the link within seconds if
    // the central keeps its default connection params: they expect a short
    // interval and a supervision timeout long enough to survive their slave
    // latency. Negotiate keyboard-friendly params before connecting.
    // Units: interval x1.25ms, timeout x10ms -> 15-30ms interval, 5s timeout.
    c->setConnectionParams(12, 24, 0, 500);

    // Async: returns as soon as the request is queued. onConnect() then asks for
    // encryption and onAuthenticationComplete() releases the setup below, so the
    // main loop keeps rendering and reading keys throughout.
    g_client = c;
    g_connecting = true;
    g_connectStart = millis();
    setStatus("Connecting...");
    if (!c->connect(g_target, true, true))
    {
        BLE_STEP("connect() request refused");
        g_connecting = false;
        g_client = nullptr;
        setStatus("Connect failed");
        if (!g_remembered.isNull())
            g_mode = MODE_RECONNECT;
        return;
    }
    BLE_STEP("connect() requested");
}

// Runs once the link is encrypted: discovery and subscription only make sense
// after bonding, and by then the peer is answering, so these are quick.
static void finishSetup()
{
    unsigned long t0 = g_connectStart;
    NimBLEClient *c = g_client;
    if (!c)
        return;

    NimBLERemoteService *hid = c->getService(UUID_HID_SERVICE);
    BLE_STEP("HID service %s", hid ? "found" : "MISSING");
    if (!hid)
    {
        setStatus("No HID service");
        c->disconnect();
        return;
    }

    // Force Boot Protocol. HID keyboards default to Report Protocol, where each
    // sends input on 0x2A4D in a device-specific layout onNotify() can't decode.
    // Writing 0 to Protocol Mode makes the keyboard emit the standard 8-byte
    // boot report (mods, reserved, six usage codes) on 0x2A22 instead.
    if (auto *pm = hid->getCharacteristic(UUID_PROTOCOL_MODE))
    {
        uint8_t boot = 0;
        pm->writeValue(&boot, 1, false); // write without response
        BLE_STEP("boot protocol written");
    }

    // Subscribe to EVERY notifiable characteristic, not just the boot report.
    // The protocol-mode write above is a write-without-response, so nothing
    // confirms the keyboard honoured it - a keyboard that exposes 0x2A22 and
    // then keeps notifying on its report-protocol characteristic leaves us
    // subscribed to a channel it never uses, connected and deaf. onNotify
    // filters by report shape, so extra subscriptions are harmless.
    int subs = 0;
    for (auto *ch : hid->getCharacteristics(true))
    {
        if (!ch->canNotify())
            continue;
        if (ch->subscribe(true, onNotify))
        {
            subs++;
            BLE_STEP("subscribed %s", ch->getUUID().toString().c_str());
        }
    }

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
    BLE_STEP("connected, %d report(s)", subs);
    g_connecting = false;
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
    if (!g_remembered.isNull())
        NimBLEDevice::deleteBond(g_remembered); // the pairing key, not just our note of it
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
    if (!g_keyQueue)
        g_keyQueue = xQueueCreate(32, sizeof(BleKeyEvent));

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
    // Apply queued key edges here, on the main loop task, not in onNotify().
    if (g_keyQueue)
    {
        BleKeyEvent ev;
        while (xQueueReceive(g_keyQueue, &ev, 0) == pdTRUE)
            keyboard_HID2Ascii(ev.code, ev.mod, ev.pressed);
    }

    if (g_wantConnect)
    {
        g_wantConnect = false;
        connectToKeyboard();
        return;
    }

    // The connect's remaining steps, each released by a NimBLE callback.
    if (g_wantSecure)
    {
        g_wantSecure = false;
        if (g_client)
        {
            _log("[blehost] secureConnection(async), bonds %d\n", NimBLEDevice::getNumBonds());
            g_client->secureConnection(true);
        }
        return;
    }
    if (g_wantSetup)
    {
        g_wantSetup = false;
        finishSetup();
        return;
    }

    // Deadline. Pairing has no timeout of its own that we can rely on - a
    // keyboard that stops answering mid-bond left the old blocking version
    // parked indefinitely - so the loop, which is now free to run, enforces one.
    // The stored key is the prime suspect, so drop it: only then can the next
    // attempt pair from scratch.
    if (g_connecting && millis() - g_connectStart > CONNECT_LIMIT_MS)
    {
        g_connecting = false;
        _log("[blehost] pairing timed out after %lums; dropping bond for %s\n",
             CONNECT_LIMIT_MS, g_target.toString().c_str());
        NimBLEDevice::deleteBond(g_target);
        if (g_client)
            g_client->disconnect();
        g_client = nullptr;
        setStatus("Pairing failed - re-pair the keyboard");
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
