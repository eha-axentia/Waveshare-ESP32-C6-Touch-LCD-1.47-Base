#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <SensorQMI8658.hpp>
#include <Wire.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

#include "board_pins.h"

// ============================================================
//  Display (ST7789 / JD9853, 172×320 IPS)
// ============================================================
static Arduino_DataBus *bus = new Arduino_ESP32SPI(
    LCD_DC, LCD_CS, LCD_SCLK, LCD_MOSI, GFX_NOT_DEFINED
);
static Arduino_GFX *gfx = new Arduino_ST7789(
    bus, LCD_RST,
    0,              // rotation set via setRotation() after panel init
    false,
    LCD_WIDTH, LCD_HEIGHT,
    LCD_COL_OFFSET, LCD_ROW_OFFSET,
    LCD_COL_OFFSET, LCD_ROW_OFFSET
);

// ============================================================
//  JD9853 panel-specific init sequence
//  Must be sent after gfx->begin(); sets MADCTL=0x00 (RGB) and
//  INVON (0x21) which are required by this panel.
// ============================================================
static void lcd_reg_init()
{
    static const uint8_t ops[] = {
        BEGIN_WRITE,
        WRITE_COMMAND_8, 0x11,
        END_WRITE,
        DELAY, 120,

        BEGIN_WRITE,
        WRITE_C8_D16, 0xDF, 0x98, 0x53,
        WRITE_C8_D8,  0xB2, 0x23,
        WRITE_COMMAND_8, 0xB7,
        WRITE_BYTES, 4, 0x00, 0x47, 0x00, 0x6F,
        WRITE_COMMAND_8, 0xBB,
        WRITE_BYTES, 6, 0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,
        WRITE_C8_D16, 0xC0, 0x44, 0xA4,
        WRITE_C8_D8,  0xC1, 0x16,
        WRITE_COMMAND_8, 0xC3,
        WRITE_BYTES, 8, 0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,
        WRITE_COMMAND_8, 0xC4,
        WRITE_BYTES, 12, 0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16,
                         0x79, 0x0B, 0x0A, 0x16, 0x82,
        WRITE_COMMAND_8, 0xC8,
        WRITE_BYTES, 32,
        0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
        0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28,
        0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,
        WRITE_COMMAND_8, 0xD0,
        WRITE_BYTES, 5, 0x04, 0x06, 0x6B, 0x0F, 0x00,
        WRITE_C8_D16, 0xD7, 0x00, 0x30,
        WRITE_C8_D8,  0xE6, 0x14,
        WRITE_C8_D8,  0xDE, 0x01,
        WRITE_COMMAND_8, 0xB7,
        WRITE_BYTES, 5, 0x03, 0x13, 0xEF, 0x35, 0x35,
        WRITE_COMMAND_8, 0xC1,
        WRITE_BYTES, 3, 0x14, 0x15, 0xC0,
        WRITE_C8_D16, 0xC2, 0x06, 0x3A,
        WRITE_C8_D16, 0xC4, 0x72, 0x12,
        WRITE_C8_D8,  0xBE, 0x00,
        WRITE_C8_D8,  0xDE, 0x02,
        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3, 0x00, 0x02, 0x00,
        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3, 0x01, 0x02, 0x00,
        WRITE_C8_D8, 0xDE, 0x00,
        WRITE_C8_D8, 0x35, 0x00,
        WRITE_C8_D8, 0x3A, 0x05,
        WRITE_COMMAND_8, 0x2A,
        WRITE_BYTES, 4, 0x00, 0x22, 0x00, 0xCD,
        WRITE_COMMAND_8, 0x2B,
        WRITE_BYTES, 4, 0x00, 0x00, 0x01, 0x3F,
        WRITE_C8_D8, 0xDE, 0x02,
        WRITE_COMMAND_8, 0xE5,
        WRITE_BYTES, 3, 0x00, 0x02, 0x00,
        WRITE_C8_D8,     0xDE, 0x00,
        WRITE_C8_D8,     0x36, 0x00,   // MADCTL: RGB order
        WRITE_COMMAND_8, 0x21,          // INVON: required by this panel
        END_WRITE,
        DELAY, 10,
        BEGIN_WRITE,
        WRITE_COMMAND_8, 0x29,
        END_WRITE
    };
    bus->batchOperation(ops, sizeof(ops));
}

// ============================================================
//  Persistent WiFi credential store
//
//  NVS namespace "wifi".  Keys:
//    count / newest — circular-buffer state
//    s0/p0 … s2/p2 — SSID / password per slot
//    tz             — POSIX timezone string
//
//  On save the slot (newest+1)%3 is overwritten; newest and
//  count are updated.  On connect, slots are tried newest-first.
// ============================================================
#define NET_SLOTS 3

static Preferences prefs;

static int netCount()  { return prefs.getInt("count",  0); }
static int netNewest() { return prefs.getInt("newest", 0); }

static bool loadNet(int slot, char *ssid, size_t ssidLen,
                              char *pass, size_t passLen)
{
    char ks[3] = {'s', (char)('0' + slot), '\0'};
    char kp[3] = {'p', (char)('0' + slot), '\0'};
    prefs.getString(ks, ssid, ssidLen);
    prefs.getString(kp, pass, passLen);
    return ssid[0] != '\0';
}

static void saveNet(const char *ssid, const char *pass)
{
    int count  = netCount();
    int newest = netNewest();
    int slot   = (newest + 1) % NET_SLOTS;

    char ks[3] = {'s', (char)('0' + slot), '\0'};
    char kp[3] = {'p', (char)('0' + slot), '\0'};
    prefs.putString(ks, ssid);
    prefs.putString(kp, pass);
    prefs.putInt("newest", slot);
    if (count < NET_SLOTS) prefs.putInt("count", count + 1);

    Serial.printf("[NVS] Saved slot %d: SSID=%s\n", slot, ssid);
}

// ============================================================
//  Timezone table
//  value = POSIX TZ string passed to configTzTime().
// ============================================================
struct TzEntry { const char *label; const char *posix; };

static const TzEntry TZ_LIST[] = {
    { "UTC",                            "UTC"                               },
    { "GMT/BST (London, Dublin)",       "GMT0BST,M3.5.0/1,M10.5.0"         },
    { "CET (Stockholm/Paris/Berlin)",   "CET-1CEST,M3.5.0,M10.5.0/3"       },
    { "EET (Helsinki/Kyiv/Tallinn)",    "EET-2EEST,M3.5.0/3,M10.5.0/4"     },
    { "MSK (Moscow)",                   "MSK-3"                             },
    { "GST (Dubai)",                    "GST-4"                             },
    { "IST (India)",                    "IST-5:30"                          },
    { "ICT (Bangkok/Jakarta)",          "ICT-7"                             },
    { "CST (Beijing/Shanghai)",         "CST-8"                             },
    { "JST (Tokyo/Seoul)",              "JST-9"                             },
    { "AEST (Sydney/Melbourne)",        "AEST-10AEDT,M10.1.0,M4.1.0/3"     },
    { "NZST (Auckland)",                "NZST-12NZDT,M9.5.0,M4.1.0/3"      },
    { "EST/EDT (New York)",             "EST5EDT,M3.2.0,M11.1.0"            },
    { "CST/CDT (Chicago)",              "CST6CDT,M3.2.0,M11.1.0"            },
    { "MST/MDT (Denver)",               "MST7MDT,M3.2.0,M11.1.0"            },
    { "PST/PDT (Los Angeles)",          "PST8PDT,M3.2.0,M11.1.0"            },
    { "AKST (Alaska)",                  "AKST9AKDT,M3.2.0,M11.1.0"          },
    { "HST (Hawaii)",                   "HST10"                             },
    { "BRT (São Paulo)",                "BRT3"                              },
    { "ART (Buenos Aires)",             "ART3"                              },
};
static constexpr int TZ_COUNT = sizeof(TZ_LIST) / sizeof(TZ_LIST[0]);

// ============================================================
//  NTP
//  configTzTime() is called on connect and every 12 h.
//  The POSIX TZ string is stored in NVS key "tz".
// ============================================================
#define NTP_RESYNC_MS (12UL * 3600UL * 1000UL)

static bool     ntpStarted = false;
static uint32_t lastNtpMs  = 0;

static void startNtp()
{
    char tz[64] = "UTC";
    prefs.getString("tz", tz, sizeof(tz));
    configTzTime(tz, "pool.ntp.org", "time.google.com");
    ntpStarted = true;
    lastNtpMs  = millis();
    Serial.printf("[NTP] configTzTime TZ=%s\n", tz);
}

// ============================================================
//  WiFi state machine
// ============================================================
enum WifiState { WS_CONNECTING, WS_CONNECTED, WS_AP };
static WifiState wifiState = WS_AP;
static bool      apRunning = false;

static char apSSID[20];   // "ESP32-C6-XXXX"
static char apPass[10];   // 8-char password (WPA2 minimum is 8)

static int      tryOrder[NET_SLOTS];
static int      tryIdx      = -1;
static char     trySSID[33] = {};
static uint32_t tryStartMs  = 0;
static uint32_t lastRetryMs = 0;

#define CONNECT_TIMEOUT_MS  10000UL
#define RETRY_INTERVAL_MS  120000UL

static void genCode(char *buf, int len)
{
    static const char C[] = "ABCDEFGHJKMNPQRSTUVWXYZ23456789";
    for (int i = 0; i < len; i++)
        buf[i] = C[esp_random() % (sizeof(C) - 1)];
    buf[len] = '\0';
}

static void startAP()
{
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAPsetHostname("esp32-c6");
    WiFi.softAP(apSSID, apPass);
    apRunning = true;
    Serial.printf("[AP] %s  pass=%s  ip=%s\n",
        apSSID, apPass, WiFi.softAPIP().toString().c_str());
}

static void stopAP()
{
    WiFi.softAPdisconnect(true);
    apRunning = false;
    Serial.println("[AP] stopped");
}

// Forward declarations for display functions called by the state machine
static void drawWifiHeader();
static void drawSplashArea();

static void beginConnectCycle()
{
    int count = netCount();
    if (count == 0) {
        wifiState = WS_AP;
        if (!apRunning) startAP();
        drawWifiHeader();
        drawSplashArea();
        return;
    }

    int newest = netNewest();
    for (int i = 0; i < NET_SLOTS; i++)
        tryOrder[i] = ((newest - i) + NET_SLOTS) % NET_SLOTS;

    tryIdx = 0;
    char pass[65];
    while (tryIdx < count) {
        if (loadNet(tryOrder[tryIdx], trySSID, sizeof(trySSID), pass, sizeof(pass)))
            break;
        tryIdx++;
    }
    if (tryIdx >= count) {
        tryIdx    = -1;
        wifiState = WS_AP;
        if (!apRunning) startAP();
        drawWifiHeader();
        drawSplashArea();
        return;
    }

    wifiState = WS_CONNECTING;
    WiFi.begin(trySSID, pass);
    tryStartMs = millis();
    Serial.printf("[WiFi] Trying %s (slot %d)\n", trySSID, tryOrder[tryIdx]);
    drawWifiHeader();
    drawSplashArea();
}

static void tickWifi(uint32_t now)
{
    // ── Actively connecting ──────────────────────────────────
    if (tryIdx >= 0) {
        if (WiFi.status() == WL_CONNECTED) {
            tryIdx    = -1;
            wifiState = WS_CONNECTED;
            if (apRunning) stopAP();
            startNtp();
            Serial.printf("[WiFi] Connected  IP=%s\n",
                WiFi.localIP().toString().c_str());
            drawWifiHeader();
            drawSplashArea();
            return;
        }
        if (now - tryStartMs >= CONNECT_TIMEOUT_MS) {
            WiFi.disconnect(true);
            tryIdx++;

            int count = netCount();
            char pass[65];
            while (tryIdx < count) {
                if (loadNet(tryOrder[tryIdx], trySSID,
                            sizeof(trySSID), pass, sizeof(pass)))
                    break;
                tryIdx++;
            }
            if (tryIdx < count) {
                WiFi.begin(trySSID, pass);
                tryStartMs = now;
                Serial.printf("[WiFi] Trying %s (slot %d)\n",
                    trySSID, tryOrder[tryIdx]);
                drawWifiHeader();
            } else {
                tryIdx    = -1;
                wifiState = WS_AP;
                if (!apRunning) startAP();
                lastRetryMs = now;
                Serial.println("[WiFi] All credentials failed, AP started");
                drawWifiHeader();
                drawSplashArea();
            }
        }
        return;
    }

    // ── Connected — watch for link loss ─────────────────────
    if (wifiState == WS_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("[WiFi] Connection lost");
            wifiState   = WS_AP;
            lastRetryMs = now;
            if (!apRunning) startAP();
            drawWifiHeader();
            drawSplashArea();
            beginConnectCycle();
        }
        return;
    }

    // ── AP mode — periodic retry ─────────────────────────────
    if (wifiState == WS_AP && netCount() > 0
            && (now - lastRetryMs >= RETRY_INTERVAL_MS)) {
        lastRetryMs = now;
        Serial.println("[WiFi] Retry timer fired");
        beginConnectCycle();
    }
}

// ============================================================
//  Web server
// ============================================================
static WebServer httpServer(80);

static void handleRoot()
{
    // In AP mode the user needs the settings page to configure WiFi.
    if (wifiState != WS_CONNECTED) {
        httpServer.sendHeader("Location", "/settings");
        httpServer.send(302);
        return;
    }

    // Build device-info strings before streaming HTML.
    char ip[16], timeStr[10], ssid[33];
    IPAddress a = WiFi.localIP();
    snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    strlcpy(ssid, WiFi.SSID().c_str(), sizeof(ssid));

    struct tm ti;
    if (getLocalTime(&ti, 0) && ti.tm_year > 120)
        snprintf(timeStr, sizeof(timeStr), "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    else
        strlcpy(timeStr, "--:--:--", sizeof(timeStr));

    httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer.send(200, "text/html", "");

    httpServer.sendContent(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32-C6-Touch</title>"
        "<style>"
        "*, *::before, *::after{box-sizing:border-box;margin:0;padding:0}"
        "body{font-family:sans-serif;background:#1a1a2e;color:#eee;"
        "display:flex;align-items:center;justify-content:center;"
        "min-height:100vh;padding:16px}"
        ".card{background:#16213e;border-radius:14px;padding:32px 36px;"
        "width:100%;max-width:360px;"
        "box-shadow:0 8px 40px rgba(0,0,0,.5);text-align:center}"
        "h1{font-size:1.55em;color:#4fc3f7;margin-bottom:4px}"
        ".sub{color:#78909c;font-size:.9em;margin-bottom:24px}"
        "dl{background:#0f3460;border-radius:8px;padding:14px 16px;"
        "text-align:left;margin-bottom:22px}"
        "dt{color:#90a4ae;font-size:.75em;text-transform:uppercase;"
        "letter-spacing:.05em;margin-top:10px}"
        "dt:first-child{margin-top:0}"
        "dd{color:#fff;font-family:monospace;font-size:.95em;margin-top:2px}"
        "a.btn{display:block;background:#1a73e8;color:#fff;"
        "text-decoration:none;padding:12px;border-radius:7px;"
        "font-size:1em;transition:background .2s}"
        "a.btn:hover{background:#1557b0}"
        "</style></head>"
        "<body><div class='card'>"
        "<h1>Hello ESP32-C6-Touch</h1>"
        "<p class='sub'>Waveshare ESP32-C6-Touch-LCD-1.47</p>"
        "<dl>"
    );

    char row[80];
    snprintf(row, sizeof(row), "<dt>IP Address</dt><dd>%s</dd>", ip);
    httpServer.sendContent(row);
    snprintf(row, sizeof(row), "<dt>Network</dt><dd>%s</dd>", ssid);
    httpServer.sendContent(row);
    snprintf(row, sizeof(row), "<dt>Time</dt><dd>%s</dd>", timeStr);
    httpServer.sendContent(row);

    httpServer.sendContent(
        "</dl>"
        "<a class='btn' href='/settings'>&#9881;&nbsp; Settings</a>"
        "</div></body></html>"
    );
}

static void handleSettings()
{
    int n = WiFi.scanNetworks();

    // Load current timezone for pre-selection
    char currentTz[64] = "UTC";
    prefs.getString("tz", currentTz, sizeof(currentTz));

    httpServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
    httpServer.send(200, "text/html", "");

    // ── Head + CSS ───────────────────────────────────────────
    httpServer.sendContent(
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>ESP32-C6 Setup</title>"
        "<style>"
        "body{font-family:sans-serif;max-width:440px;margin:20px auto;padding:0 12px}"
        "h1{font-size:1.3em;margin-bottom:4px}"
        "h2{font-size:1em;border-bottom:1px solid #ccc;padding-bottom:4px;margin-top:1.4em}"
        ".badge{padding:5px 10px;border-radius:4px;display:inline-block;margin-bottom:10px}"
        ".ok{background:#d4edda;color:#155724}"
        ".ap{background:#fff3cd;color:#856404}"
        "label{display:block;margin-top:10px;font-weight:bold}"
        "select,input[type=password]{"
        "width:100%;padding:8px;margin-top:4px;"
        "box-sizing:border-box;font-size:1em;"
        "border:1px solid #ccc;border-radius:4px}"
        "button{width:100%;padding:10px;background:#1a73e8;color:#fff;"
        "border:none;border-radius:4px;font-size:1em;cursor:pointer;margin-top:12px}"
        "ul{padding-left:18px}li{margin:3px 0}"
        ".hint{color:#666;font-size:.85em;margin-top:6px}"
        "</style></head><body>"
        "<h1>ESP32-C6 Setup</h1>"
    );

    // ── Status badge ─────────────────────────────────────────
    if (wifiState == WS_CONNECTED) {
        char ip[16];
        IPAddress a = WiFi.localIP();
        snprintf(ip, sizeof(ip), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
        char badge[80];
        snprintf(badge, sizeof(badge),
            "<span class='badge ok'>&#10003; Connected &mdash; %s</span>", ip);
        httpServer.sendContent(badge);
    } else {
        httpServer.sendContent(
            "<span class='badge ap'>&#9679; AP active &mdash; 192.168.4.1</span>");
    }

    // ── Saved networks ───────────────────────────────────────
    httpServer.sendContent("<h2>Saved Networks</h2><ul>");
    int count  = netCount();
    int newest = netNewest();
    if (count == 0) {
        httpServer.sendContent("<li><em>None</em></li>");
    } else {
        for (int i = 0; i < count; i++) {
            int slot = ((newest - i) + NET_SLOTS) % NET_SLOTS;
            char ssid[33], pass[65];
            if (loadNet(slot, ssid, sizeof(ssid), pass, sizeof(pass))) {
                char li[80];
                snprintf(li, sizeof(li), "<li>%s%s</li>",
                    ssid, i == 0 ? " <em>(newest)</em>" : "");
                httpServer.sendContent(li);
            }
        }
    }
    httpServer.sendContent("</ul>");

    // ── Add network form ─────────────────────────────────────
    httpServer.sendContent(
        "<h2>Add Network</h2>"
        "<form method='POST' action='/save'>"
        "<label>Network:</label>"
        "<select name='ssid'>"
    );
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < i; j++)
            if (WiFi.SSID(i) == WiFi.SSID(j)) { dup = true; break; }
        if (dup) continue;
        char opt[80];
        snprintf(opt, sizeof(opt), "<option>%s</option>",
            WiFi.SSID(i).c_str());
        httpServer.sendContent(opt);
    }
    WiFi.scanDelete();
    httpServer.sendContent(
        "</select>"
        "<label>Password:</label>"
        "<input type='password' name='pass' "
        "placeholder='Leave empty for open networks'>"
        "<button type='submit'>Save &amp; Connect</button>"
        "</form>"
        "<p class='hint'>Stores up to 3 networks (oldest replaced). "
        "AP shuts down when a connection succeeds.</p>"
    );

    // ── Time zone form ───────────────────────────────────────
    httpServer.sendContent(
        "<h2>Time Zone</h2>"
        "<form method='POST' action='/savetz'>"
        "<label>Time zone:</label>"
        "<select name='tz'>"
    );
    for (int i = 0; i < TZ_COUNT; i++) {
        char opt[120];
        bool sel = (strcmp(currentTz, TZ_LIST[i].posix) == 0);
        snprintf(opt, sizeof(opt), "<option value='%s'%s>%s</option>",
            TZ_LIST[i].posix, sel ? " selected" : "", TZ_LIST[i].label);
        httpServer.sendContent(opt);
    }
    httpServer.sendContent(
        "</select>"
        "<button type='submit'>Save Time Zone</button>"
        "</form>"
        "<p class='hint'>NTP syncs on connect and every 12 h. "
        "Clock shows 24 h local time in the display header.</p>"
        "</body></html>"
    );
}

static void handleSave()
{
    String ssid = httpServer.arg("ssid");
    String pass = httpServer.arg("pass");
    if (ssid.length() > 0) {
        saveNet(ssid.c_str(), pass.c_str());
        beginConnectCycle();
    }
    httpServer.sendHeader("Location", "/settings");
    httpServer.send(303);
}

static void handleSaveTz()
{
    String tz = httpServer.arg("tz");
    if (tz.length() > 0 && tz.length() < 64) {
        prefs.putString("tz", tz.c_str());
        Serial.printf("[TZ] Saved: %s\n", tz.c_str());
        if (wifiState == WS_CONNECTED) startNtp();
    }
    httpServer.sendHeader("Location", "/settings");
    httpServer.send(303);
}

// ============================================================
//  Display layout
//
//  y=  0 ┌─────────────────────────────────┬────────────────┐
//        │ ● SSID (≤15 chars)              │   HH:MM:SS     │ y=2
//        │   IP address                    │                │ y=14
//  y= 26 ├─────────────────────────────────┴────────────────┤
//  y= 28 │  [Connected]  ESP32-C6 / Touch LCD               │
//        │  [AP mode]    WiFi Config AP / SSID / Pass / URL  │
//        │  [Connecting] Connecting to: <SSID>               │
//  y= 76 ├───────────────────────────────────────────────────┤
//  y= 86 │  Accel + Gyro labels & values                     │
//  y=188 ├───────────────────────────────────────────────────┤
//  y=198 │  Touch: tap to draw                               │
//  y=210 ├───────────────────────────────────────────────────┤
//  y=213    touch drawing area                            y=317
// ============================================================
#define HEADER_SEP_Y  26
#define SPLASH_SEP_Y  76

// Clock occupies the top-right corner of the header.
// "HH:MM:SS" = 8 chars × 6 px = 48 px wide; leave 2 px right margin.
#define CLOCK_W   48
#define CLOCK_X   (LCD_WIDTH - CLOCK_W - 2)   // 122
#define CLOCK_Y   2
// SSID is capped at 15 chars to avoid overlapping the clock.
#define SSID_MAXC 15

static void drawClock()
{
    struct tm ti;
    char buf[10];
    if (getLocalTime(&ti, 0) && ti.tm_year > 120) {
        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                 ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        strlcpy(buf, "--:--:--", sizeof(buf));
    }
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setTextSize(1);
    gfx->setCursor(CLOCK_X, CLOCK_Y);
    gfx->print(buf);
}

static void drawWifiHeader()
{
    gfx->fillRect(0, 0, LCD_WIDTH, HEADER_SEP_Y, RGB565_BLACK);

    uint16_t dotColor;
    char row1[SSID_MAXC + 1] = {};
    char row2[18]             = {};

    if (tryIdx >= 0) {
        dotColor = RGB565_YELLOW;
        strlcpy(row1, trySSID, sizeof(row1));
        strlcpy(row2, "Connecting...", sizeof(row2));
    } else if (wifiState == WS_CONNECTED) {
        dotColor = RGB565_GREEN;
        strlcpy(row1, WiFi.SSID().c_str(), sizeof(row1));
        IPAddress a = WiFi.localIP();
        snprintf(row2, sizeof(row2), "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
    } else {
        dotColor = RGB565_YELLOW;
        strlcpy(row1, apSSID, sizeof(row1));
        strlcpy(row2, "192.168.4.1", sizeof(row2));
    }

    gfx->fillCircle(6, 6, 4, dotColor);
    gfx->setTextSize(1);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setCursor(14, 2);  gfx->print(row1);
    gfx->setCursor(14, 14); gfx->print(row2);
    gfx->drawFastHLine(0, HEADER_SEP_Y, LCD_WIDTH, RGB565_DARKGREY);

    drawClock();    // always paint clock after clearing header
}

static void drawSplashArea()
{
    gfx->fillRect(0, HEADER_SEP_Y + 2, LCD_WIDTH,
                  SPLASH_SEP_Y - HEADER_SEP_Y - 2, RGB565_BLACK);

    if (wifiState == WS_CONNECTED) {
        gfx->setTextColor(RGB565_CYAN);
        gfx->setTextSize(2);
        gfx->setCursor(6, 34); gfx->print("ESP32-C6");
        gfx->setCursor(6, 54); gfx->print("Touch LCD");
    } else if (wifiState == WS_AP && tryIdx < 0) {
        gfx->setTextColor(RGB565_WHITE);
        gfx->setTextSize(1);
        gfx->setCursor(6, 32); gfx->print("WiFi Config AP");
        gfx->setTextColor(RGB565_YELLOW);
        gfx->setCursor(6, 44); gfx->print("SSID: "); gfx->print(apSSID);
        gfx->setCursor(6, 56); gfx->print("Pass: "); gfx->print(apPass);
        gfx->setTextColor(RGB565_GREEN);
        gfx->setCursor(6, 68); gfx->print("192.168.4.1/settings");
    } else {
        gfx->setTextColor(RGB565_WHITE);
        gfx->setTextSize(1);
        gfx->setCursor(6, 40); gfx->print("Connecting to:");
        gfx->setTextColor(RGB565_YELLOW);
        gfx->setCursor(6, 54); gfx->print(trySSID);
    }

    gfx->drawFastHLine(0, SPLASH_SEP_Y, LCD_WIDTH, RGB565_DARKGREY);
}

// ============================================================
//  IMU — QMI8658A
// ============================================================
static SensorQMI8658 imu;
static bool imuReady = false;

// ============================================================
//  Touch — AXS5106L  (5-byte burst from reg 0x01)
// ============================================================
struct TouchPoint { int16_t x, y; bool pressed; };

static void touchReset()
{
    digitalWrite(TOUCH_RST, LOW);  delay(200);
    digitalWrite(TOUCH_RST, HIGH); delay(300);
}

static TouchPoint readTouch()
{
    TouchPoint tp = {0, 0, false};
    Wire.beginTransmission(TOUCH_ADDR);
    Wire.write(0x01);
    if (Wire.endTransmission() != 0) return tp;
    if (Wire.requestFrom((uint8_t)TOUCH_ADDR, (uint8_t)14) < 14) return tp;

    uint8_t data[14];
    for (int i = 0; i < 14; i++) data[i] = Wire.read();

    if (data[1] > 0) {
        tp.x       = (int16_t)(((data[2] & 0x0F) << 8) | data[3]);
        tp.y       = (int16_t)(((data[4] & 0x0F) << 8) | data[5]);
        tp.pressed = true;
    }
    return tp;
}

// ============================================================
//  Display helpers
// ============================================================
static void drawValue(int16_t x, int16_t y, float v)
{
    char buf[12];
    snprintf(buf, sizeof(buf), "%+7.2f", v);
    gfx->setTextColor(RGB565_WHITE, RGB565_BLACK);
    gfx->setTextSize(1);
    gfx->setCursor(x, y);
    gfx->print(buf);
}

// ============================================================
//  Setup
// ============================================================
void setup()
{
    Serial.begin(115200);
    Serial.println("\n=== Waveshare ESP32-C6-Touch-LCD-1.47 ===");

    // --- Backlight -------------------------------------------
    pinMode(LCD_BL, OUTPUT);
    digitalWrite(LCD_BL, HIGH);

    // --- Display init ----------------------------------------
    if (!gfx->begin()) {
        Serial.println("[ERROR] Display init failed");
        while (true) delay(1000);
    }
    lcd_reg_init();
    gfx->setRotation(0);
    gfx->fillScreen(RGB565_BLACK);

    // --- Static IMU panel labels -----------------------------
    gfx->setTextColor(RGB565_YELLOW);
    gfx->setTextSize(1);
    gfx->setCursor(6, 86);  gfx->print("-- Accelerometer (g) --");
    gfx->setCursor(6, 98);  gfx->print("X:");
    gfx->setCursor(6, 110); gfx->print("Y:");
    gfx->setCursor(6, 122); gfx->print("Z:");
    gfx->setCursor(6, 138); gfx->print("--- Gyroscope (dps) ---");
    gfx->setCursor(6, 150); gfx->print("X:");
    gfx->setCursor(6, 162); gfx->print("Y:");
    gfx->setCursor(6, 174); gfx->print("Z:");
    gfx->drawFastHLine(0, 188, LCD_WIDTH, RGB565_DARKGREY);
    gfx->setTextColor(RGB565_GREEN);
    gfx->setCursor(6, 198); gfx->print("Touch: tap to draw");
    gfx->drawFastHLine(0, 210, LCD_WIDTH, RGB565_DARKGREY);

    // --- NVS -------------------------------------------------
    prefs.begin("wifi", false);

    // --- Generate AP credentials (fresh each boot) -----------
    char suffix[5], pass[9];
    genCode(suffix, 4);
    genCode(pass,   8);
    snprintf(apSSID, sizeof(apSSID), "ESP32-C6-%s", suffix);
    strlcpy(apPass, pass, sizeof(apPass));

    // --- WiFi ------------------------------------------------
    WiFi.setHostname("esp32-c6");
    startAP();
    drawWifiHeader();
    drawSplashArea();

    beginConnectCycle();

    // --- Web server ------------------------------------------
    httpServer.on("/",         HTTP_GET,  handleRoot);
    httpServer.on("/settings", HTTP_GET,  handleSettings);
    httpServer.on("/save",     HTTP_POST, handleSave);
    httpServer.on("/savetz",   HTTP_POST, handleSaveTz);
    httpServer.begin();
    Serial.println("[OK] Web server started");

    // --- I2C + Touch -----------------------------------------
    Wire.begin(IMU_SDA, IMU_SCL);
    pinMode(TOUCH_RST, OUTPUT);
    touchReset();
    Serial.println("[OK] Touch ready");

    // --- IMU -------------------------------------------------
    imuReady = imu.init(Wire, IMU_ADDR);
    if (imuReady) {
        imu.configAccelerometer(SensorQMI8658::ACC_RANGE_4G,
                                SensorQMI8658::ACC_ODR_250Hz);
        imu.configGyroscope(SensorQMI8658::GYR_RANGE_64DPS,
                            SensorQMI8658::GYR_ODR_224_2Hz);
        imu.enableAccelerometer();
        imu.enableGyroscope();
        Serial.printf("[OK] QMI8658A  addr=0x%02X\n", IMU_ADDR);
    } else {
        Serial.println("[WARN] QMI8658A not found");
        gfx->setTextColor(RGB565_RED);
        gfx->setCursor(6, 98);
        gfx->print("IMU not found");
    }
}

// ============================================================
//  Loop
// ============================================================
void loop()
{
    static uint32_t lastImu   = 0;
    static uint32_t lastTouch = 0;
    static uint32_t lastClock = 0;

    uint32_t now = millis();

    tickWifi(now);
    httpServer.handleClient();

    // --- Clock: update every second --------------------------
    if (now - lastClock >= 1000) {
        lastClock = now;
        drawClock();
    }

    // --- NTP re-sync every 12 h when connected ---------------
    if (ntpStarted && wifiState == WS_CONNECTED
            && (now - lastNtpMs) >= NTP_RESYNC_MS) {
        startNtp();
    }

    // --- IMU every 100 ms ------------------------------------
    if (imuReady && (now - lastImu >= 100)) {
        lastImu = now;
        if (imu.getDataReady()) {
            float ax, ay, az, gx, gy, gz;
            imu.getAccelerometer(ax, ay, az);
            imu.getGyroscope(gx, gy, gz);
            drawValue(18, 98,  ax); drawValue(18, 110, ay); drawValue(18, 122, az);
            drawValue(18, 150, gx); drawValue(18, 162, gy); drawValue(18, 174, gz);
            Serial.printf("Accel %+6.2f %+6.2f %+6.2f  Gyro %+7.2f %+7.2f %+7.2f\n",
                          ax, ay, az, gx, gy, gz);
        }
    }

    // --- Touch every 20 ms -----------------------------------
    if (now - lastTouch >= 20) {
        lastTouch = now;
        TouchPoint tp = readTouch();
        if (tp.pressed) {
            Serial.printf("Touch  x=%-4d y=%-4d\n", tp.x, tp.y);
            int16_t px = (int16_t)((long)tp.x * LCD_WIDTH  / 4096);
            int16_t py = (int16_t)((long)tp.y * LCD_HEIGHT / 4096);
            px = constrain(px, 2,   LCD_WIDTH  - 3);
            py = constrain(py, 213, LCD_HEIGHT - 3);
            gfx->fillCircle(px, py, 3, RGB565_MAGENTA);
        }
    }
}
