#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <ArduinoJson.h>
#include <Preferences.h>
#include "test_map_jpg.h"

// ============================================================================
// SPIDEY-NAV : COCKPIT HUD & DUAL-STREAM RECEIVER (WI-FI + SERIAL)
// ============================================================================
// Platform: ESP32-C3 Super Mini + 2.4" SPI TFT (ILI9341 320x240)
// Protocol 1: TCP Port 8080 - High-Speed JSON Stream from Flutter Companion
// Protocol 2: HTTP Port 80   - Binary JPEG /map Stream (Web / Python Testbench)
// Protocol 3: USB-CDC Serial - High-Speed Binary Stream (0xAA 0x55 [H] [L] [JPEG])
// NVS Storage: Stores Hotspot SSID & Password across motorcycle power cycles
// ============================================================================

TFT_eSPI tft = TFT_eSPI();
WiFiServer tcpMapServer(80);
WiFiServer hudServer(8080);
WiFiClient g_hudClient;
Preferences g_prefs;

// --- 16-bit 565 Colors (Strict 3-Tone Spidey Palette) ---
constexpr uint16_t COLOR_BG_NAVY     = 0x08A5; // #0D1527 Deep Obsidian Midnight
constexpr uint16_t COLOR_SPIDEY_RED  = 0xE104; // #E62429 Crimson Red
constexpr uint16_t COLOR_TEXT_WHITE  = 0xFFFF; // #F0F4F8 Pure Crisp White
constexpr uint16_t COLOR_EMERALD     = 0x07E0; // #00E676 GPS Locked / Net Connected
constexpr uint16_t COLOR_AMBER       = 0xFD20; // #FFAB00 GPS Searching / Connecting
constexpr uint16_t COLOR_CYAN        = 0x07FF; // Cyan Accent
constexpr uint16_t COLOR_CARD_BORDER = 0x2128; // Subtle glass outline
constexpr uint16_t COLOR_WEB_LINE    = 0x31E9; // Dim web vector grid

// --- Static Buffer untuk JPEG Masuk ---
constexpr size_t MAX_JPG_BUFFER = 32768;
static uint8_t   g_rxBuffer[MAX_JPG_BUFFER];

// --- Hardware Input Pins ---
constexpr uint8_t HW_BTN_MODE   = 0; // GPIO 0 (Boot button Super Mini) -> Toggle Rotasi Layar
constexpr uint8_t HW_BTN_ACTION = 1; // GPIO 1 -> Toggle Mode Malam (Invert)
constexpr uint8_t HW_BTN_STRESS = 4; // GPIO 4 -> Diagnostics
constexpr uint8_t HW_LED_SYS    = 8; // Onboard LED (Active LOW)

// --- State Navigasi HUD ---
struct HudState {
    int  speed        = 0;
    int  heading      = 0;
    int  gpsStatus    = 0;   // 0: searching, 1: locked, 2: offline
    int  turnCode     = 255; // 255: no route / standby
    int  distMeters   = 0;
    int  idleMode     = 0;   // 0: eyes, 1: web, 2: sonar, 3: speedo
    bool isPowered    = true;
    bool hasNewPacket = false;
    uint32_t lastPacketMs = 0;
};

static HudState g_hud;
static uint8_t  g_rotation        = 1; // 1: Landscape (default), 0: Portrait
static bool     g_idleStaticDrawn = false;
static uint32_t g_lastIdleTickMs  = 0;
static uint32_t g_lastMapFrameMs  = 0;
static bool     g_mapModeActive   = false;
static float    g_measuredFps     = 0.0f;
static uint32_t g_frameCount      = 0;
static uint32_t g_lastFrameMs     = 0;

static String g_activeSsid = "logolo";
static String g_activePass = "rpl11rpl";
static wl_status_t g_lastWifiStatus = WL_IDLE_STATUS;

// TJpg_Decoder Callback: Direct MCU Stream to SPI
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

// Button Debouncer
class ButtonDebouncer {
public:
    explicit ButtonDebouncer(uint8_t pin) : m_pin(pin) {}
    void begin() { pinMode(m_pin, INPUT_PULLUP); }
    bool wasPressed() {
        bool raw = digitalRead(m_pin);
        uint32_t now = millis();
        if (raw != m_lastRaw) {
            m_lastDebounce = now;
            m_lastRaw = raw;
        }
        if ((now - m_lastDebounce) > 40) {
            if (raw != m_state) {
                m_state = raw;
                if (m_state == LOW) return true;
            }
        }
        return false;
    }
private:
    uint8_t  m_pin;
    bool     m_state = HIGH;
    bool     m_lastRaw = HIGH;
    uint32_t m_lastDebounce = 0;
};

static ButtonDebouncer btnBenchmark(HW_BTN_MODE);
static ButtonDebouncer btnOverlay(HW_BTN_ACTION);
static ButtonDebouncer btnStress(HW_BTN_STRESS);

// ============================================================================
// CYBERNETIC HUD & SPIDER WEB GEOMETRY (Drawn once per mode change)
// ============================================================================

void drawCyberHudFrame() {
    int w = tft.width();
    int h = tft.height();

    // 1. Angular Corner Brackets (Spidey Crimson)
    int bl = 24; // bracket length
    // Top-Left
    tft.drawLine(8, 8, 8 + bl, 8, COLOR_SPIDEY_RED);
    tft.drawLine(8, 8, 8, 8 + bl, COLOR_SPIDEY_RED);
    tft.drawPixel(10, 10, COLOR_TEXT_WHITE);

    // Top-Right
    tft.drawLine(w - 9, 8, w - 9 - bl, 8, COLOR_SPIDEY_RED);
    tft.drawLine(w - 9, 8, w - 9, 8 + bl, COLOR_SPIDEY_RED);
    tft.drawPixel(w - 11, 10, COLOR_TEXT_WHITE);

    // Bottom-Left
    tft.drawLine(8, h - 9, 8 + bl, h - 9, COLOR_SPIDEY_RED);
    tft.drawLine(8, h - 9, 8, h - 9 - bl, COLOR_SPIDEY_RED);
    tft.drawPixel(10, h - 11, COLOR_TEXT_WHITE);

    // Bottom-Right
    tft.drawLine(w - 9, h - 9, w - 9 - bl, h - 9, COLOR_SPIDEY_RED);
    tft.drawLine(w - 9, h - 9, w - 9, h - 9 - bl, COLOR_SPIDEY_RED);
    tft.drawPixel(w - 11, h - 11, COLOR_TEXT_WHITE);

    // 2. Spider Web Geometry: 4 Diagonal Guide Lines (Stop before eyes!)
    tft.drawLine(14, 14, 80, 60, COLOR_WEB_LINE);
    tft.drawLine(80, 60, 95, 60, COLOR_WEB_LINE);

    tft.drawLine(w - 15, 14, w - 81, 60, COLOR_WEB_LINE);
    tft.drawLine(w - 81, 60, w - 96, 60, COLOR_WEB_LINE);

    tft.drawLine(14, h - 15, 80, h - 55, COLOR_WEB_LINE);
    tft.drawLine(80, h - 55, 95, h - 55, COLOR_WEB_LINE);

    tft.drawLine(w - 15, h - 15, w - 81, h - 55, COLOR_WEB_LINE);
    tft.drawLine(w - 81, h - 55, w - 96, h - 55, COLOR_WEB_LINE);

    // Concentric Web Arc Segments (Subtle framing)
    tft.drawLine(40, 24, 75, 24, COLOR_WEB_LINE);
    tft.drawLine(24, 40, 24, 75, COLOR_WEB_LINE);
    tft.drawLine(w - 41, 24, w - 76, 24, COLOR_WEB_LINE);
    tft.drawLine(w - 25, 40, w - 25, 75, COLOR_WEB_LINE);

    // 3. Top Header Bar
    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_NAVY);
    tft.drawString("SPIDEY-NAV COCKPIT", w / 2, 10, 2);
    tft.fillCircle(w / 2 - 82, 16, 3, COLOR_SPIDEY_RED);
    tft.fillCircle(w / 2 + 82, 16, 3, COLOR_SPIDEY_RED);

    // 4. Bottom Connectivity Info Card
    int pillW = min(290, w - 20);
    int pillH = 38;
    int pillX = (w - pillW) / 2;
    int pillY = h - pillH - 8;

    tft.fillRoundRect(pillX, pillY, pillW, pillH, 8, 0x1105);
    tft.drawRoundRect(pillX, pillY, pillW, pillH, 8, COLOR_CARD_BORDER);

    tft.setTextDatum(MC_DATUM);

    if (WiFi.status() == WL_CONNECTED) {
        char ipStr[64];
        snprintf(ipStr, sizeof(ipStr), "IP: %s:8080", WiFi.localIP().toString().c_str());
        tft.setTextColor(COLOR_EMERALD, 0x1105);
        tft.drawString(ipStr, w / 2, pillY + 11, 2);

        char netStr[64];
        snprintf(netStr, sizeof(netStr), "NET: %s (INTERNET AKTIF)", g_activeSsid.c_str());
        tft.setTextColor(COLOR_CYAN, 0x1105);
        tft.drawString(netStr, w / 2, pillY + 26, 1);
    } else {
        if (WiFi.softAPgetStationNum() > 0) {
            tft.setTextColor(COLOR_EMERALD, 0x1105);
            tft.drawString("IP: 192.168.4.1:8080", w / 2, pillY + 11, 2);

            tft.setTextColor(COLOR_CYAN, 0x1105);
            tft.drawString("HP TERHUBUNG KE SPIDEY-AP", w / 2, pillY + 26, 1);
        } else {
            tft.setTextColor(COLOR_CYAN, 0x1105);
            tft.drawString("WIFI: SpideyNav-AP | PORT: 8080", w / 2, pillY + 11, 2);

            tft.setTextColor(0x7BEF, 0x1105);
            tft.drawString("MENUNGGU RUTE DARI HP...", w / 2, pillY + 26, 1);
        }
    }
}

// ============================================================================
// HUD VECTOR RENDERING ENGINE (Flicker-Free, No Full Screen Refresh)
// ============================================================================

void drawTurnArrow(int trn, int cx, int cy) {
    tft.fillRect(cx - 50, cy - 50, 100, 100, COLOR_BG_NAVY);

    switch (trn) {
        case 0: // Straight
            tft.fillTriangle(cx, cy - 42, cx - 28, cy - 10, cx + 28, cy - 10, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 12, cy - 10, 24, 46, COLOR_SPIDEY_RED);
            break;
        case 1: // Left 90 deg
            tft.fillTriangle(cx - 42, cy, cx - 12, cy - 28, cx - 12, cy + 28, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 12, cy - 12, 44, 24, COLOR_SPIDEY_RED);
            tft.fillRect(cx + 10, cy - 12, 22, 46, COLOR_SPIDEY_RED);
            break;
        case 2: // Right 90 deg
            tft.fillTriangle(cx + 42, cy, cx + 12, cy - 28, cx + 12, cy + 28, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 32, cy - 12, 44, 24, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 32, cy - 12, 22, 46, COLOR_SPIDEY_RED);
            break;
        case 3: // Slight Left
            tft.fillTriangle(cx - 35, cy - 35, cx - 10, cy - 45, cx - 45, cy - 10, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 10, cy - 10, 20, 42, COLOR_SPIDEY_RED);
            break;
        case 4: // Slight Right
            tft.fillTriangle(cx + 35, cy - 35, cx + 10, cy - 45, cx + 45, cy - 10, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 10, cy - 10, 20, 42, COLOR_SPIDEY_RED);
            break;
        case 5: // Sharp Left
            tft.fillTriangle(cx - 40, cy + 10, cx - 15, cy - 15, cx - 15, cy + 35, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 15, cy - 25, 45, 20, COLOR_SPIDEY_RED);
            tft.fillRect(cx + 15, cy - 25, 20, 55, COLOR_SPIDEY_RED);
            break;
        case 6: // Sharp Right
            tft.fillTriangle(cx + 40, cy + 10, cx + 15, cy - 15, cx + 15, cy + 35, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 30, cy - 25, 45, 20, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 35, cy - 25, 20, 55, COLOR_SPIDEY_RED);
            break;
        case 7: // U-Turn
            tft.fillCircle(cx, cy - 15, 32, COLOR_SPIDEY_RED);
            tft.fillCircle(cx, cy - 15, 14, COLOR_BG_NAVY);
            tft.fillRect(cx - 32, cy - 15, 64, 45, COLOR_BG_NAVY);
            tft.fillRect(cx + 14, cy - 15, 18, 48, COLOR_SPIDEY_RED);
            tft.fillRect(cx - 32, cy - 15, 18, 30, COLOR_SPIDEY_RED);
            tft.fillTriangle(cx - 23, cy + 32, cx - 42, cy + 12, cx - 4, cy + 12, COLOR_SPIDEY_RED);
            break;
        case 8: // Arrived
            tft.fillCircle(cx, cy - 10, 24, COLOR_SPIDEY_RED);
            tft.fillCircle(cx, cy - 10, 10, COLOR_TEXT_WHITE);
            tft.fillTriangle(cx, cy + 28, cx - 18, cy, cx + 18, cy, COLOR_SPIDEY_RED);
            break;
        default:
            tft.drawCircle(cx, cy, 26, COLOR_SPIDEY_RED);
            tft.fillCircle(cx, cy, 8, COLOR_TEXT_WHITE);
            break;
    }
}

void renderActiveNavHUD() {
    int w = tft.width();
    int h = tft.height();
    int cx = w / 2;
    int cy = h / 2;

    static int s_lastTrn = -1;
    static int s_lastDist = -1;
    static int s_lastSpd = -1;
    static int s_lastGps = -1;

    // Header Card: Jarak manuver belokan
    if (g_hud.distMeters != s_lastDist) {
        s_lastDist = g_hud.distMeters;
        tft.fillRoundRect(12, 10, w - 24, 44, 12, 0x1105);
        tft.drawRoundRect(12, 10, w - 24, 44, 12, COLOR_SPIDEY_RED);

        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COLOR_TEXT_WHITE, 0x1105);
        tft.setTextSize(1);
        char distStr[32];
        if (s_lastDist < 1000) {
            snprintf(distStr, sizeof(distStr), "%d METER MENUJU BELOKAN", s_lastDist);
        } else {
            snprintf(distStr, sizeof(distStr), "%.1f KM MENUJU BELOKAN", s_lastDist / 1000.0f);
        }
        tft.drawString(distStr, cx, 32, 4);
    }

    // Center: Panah Belokan Spidey
    if (g_hud.turnCode != s_lastTrn) {
        s_lastTrn = g_hud.turnCode;
        drawTurnArrow(s_lastTrn, cx, cy);
    }

    // Bottom Speedometer & GPS Status Bar
    if (g_hud.speed != s_lastSpd || g_hud.gpsStatus != s_lastGps) {
        s_lastSpd = g_hud.speed;
        s_lastGps = g_hud.gpsStatus;

        int boxW = (w - 32) / 2;
        // Speedometer Box (Kiri Bawah)
        tft.fillRoundRect(12, h - 58, boxW, 48, 10, 0x1105);
        tft.drawRoundRect(12, h - 58, boxW, 48, 10, COLOR_CARD_BORDER);

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COLOR_TEXT_WHITE, 0x1105);
        char spdStr[16];
        snprintf(spdStr, sizeof(spdStr), "%d", s_lastSpd);
        tft.drawString(spdStr, 24, h - 34, 6);
        tft.drawString("KM/H", 24 + 68, h - 32, 2);

        // GPS & Status Pill (Kanan Bawah)
        int rightX = 12 + boxW + 8;
        tft.fillRoundRect(rightX, h - 58, boxW, 48, 10, 0x1105);
        tft.drawRoundRect(rightX, h - 58, boxW, 48, 10, COLOR_CARD_BORDER);

        tft.setTextDatum(MC_DATUM);
        if (s_lastGps == 1) {
            tft.fillCircle(rightX + 18, h - 34, 6, COLOR_EMERALD);
            tft.setTextColor(COLOR_EMERALD, 0x1105);
            tft.drawString("GPS LOCKED", rightX + boxW / 2 + 10, h - 34, 2);
        } else {
            tft.fillCircle(rightX + 18, h - 34, 6, COLOR_AMBER);
            tft.setTextColor(COLOR_AMBER, 0x1105);
            tft.drawString("SEARCHING...", rightX + boxW / 2 + 10, h - 34, 2);
        }
    }
}

// ============================================================================
// STANDBY IDLE ANIMATIONS (Spider Eyes, Web Radar, Pulse Sonar, Speedo)
// ============================================================================

void drawAuthenticSpideyEye(int cx, int cy, int eyeH, bool isLeft) {
    int sign = isLeft ? -1 : 1;

    // Authentic Spidey Eyepiece Geometry:
    int p1x = cx + sign * 54;
    int p1y = cy - eyeH;

    int p2x = cx + sign * 8;
    int p2y = cy + 2;

    int p3x = cx + sign * 44;
    int p3y = cy + (int)(eyeH * 0.85f);

    // Thick crimson outer contour
    tft.fillTriangle(p1x, p1y, p2x, p2y, p3x, p3y, COLOR_SPIDEY_RED);

    // Inner glowing white lens
    int insetX = sign * 4;
    int insetY = 3;
    tft.fillTriangle(p1x - insetX, p1y + insetY,
                     p2x - insetX, p2y - 1,
                     p3x - insetX, p3y - insetY,
                     COLOR_TEXT_WHITE);

    // Sharp outer border outline
    tft.drawTriangle(p1x, p1y, p2x, p2y, p3x, p3y, COLOR_TEXT_WHITE);
}

void renderIdleScreen(int mode, float t) {
    int w = tft.width();
    int h = tft.height();
    int cx = w / 2;
    int cy = h / 2 - 4;

    // Gambar frame cybernetic & web garis sekali saja per mode
    if (!g_idleStaticDrawn) {
        tft.fillScreen(COLOR_BG_NAVY);
        drawCyberHudFrame();
        g_idleStaticDrawn = true;
    }

    switch (mode) {
        case 0: { // Spider Eyes Blinking (Authentic Spider Mask Shape)
            float blink = (sinf(t * 6.28f) * 0.5f + 0.5f);
            int eyeH = 14 + (int)(blink * 16.0f);

            // Bersihkan hanya bounding box mata (tidak menghapus garis web di sekelilingnya!)
            tft.fillRect(cx - 68, cy - 35, 136, 72, COLOR_BG_NAVY);

            // Left Eye
            drawAuthenticSpideyEye(cx, cy, eyeH, true);
            // Right Eye
            drawAuthenticSpideyEye(cx, cy, eyeH, false);
            break;
        }
        case 1: { // Expanding Spider Web Radar
            static int lastR = 0;
            int r = (int)(fmodf(t * 30.0f, 40.0f)) + 12;

            if (abs(r - lastR) > 3) {
                tft.fillCircle(cx, cy, 60, COLOR_BG_NAVY);
                for (int i = 0; i < 8; i++) {
                    float rad = i * (3.14159f / 4.0f);
                    tft.drawLine(cx, cy, cx + (int)(cosf(rad) * 55), cy + (int)(sinf(rad) * 55), 0x4228);
                }
                tft.drawCircle(cx, cy, r, COLOR_SPIDEY_RED);
                if (r > 20) tft.drawCircle(cx, cy, r - 12, COLOR_SPIDEY_RED);
                lastR = r;
            }
            break;
        }
        case 2: { // Sonar Pulse
            static int lastP = 0;
            int p = (int)(fmodf(t * 35.0f, 48.0f)) + 6;
            if (abs(p - lastP) > 3) {
                tft.fillCircle(cx, cy, 55, COLOR_BG_NAVY);
                tft.drawCircle(cx, cy, p, COLOR_SPIDEY_RED);
                if (p > 18) tft.drawCircle(cx, cy, p - 16, 0x8902);
                tft.fillCircle(cx, cy, 6, COLOR_TEXT_WHITE);
                lastP = p;
            }
            break;
        }
        default: { // Digital HUD Standby
            tft.fillRect(cx - 70, cy - 30, 140, 60, COLOR_BG_NAVY);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_NAVY);
            tft.drawString("SPIDEY HUD", cx, cy - 14, 4);
            tft.setTextColor(COLOR_SPIDEY_RED, COLOR_BG_NAVY);
            tft.drawString("STANDBY", cx, cy + 16, 4);
            break;
        }
    }
}

// ============================================================================
// JPEG MAP STREAM ENGINE (From Flash, HTTP /map, or USB Serial)
// ============================================================================

void renderMapFromBuffer(const uint8_t* buf, size_t len) {
    g_mapModeActive = true;
    g_lastMapFrameMs = millis();

    uint32_t t0 = millis();
    TJpgDec.drawJpg(0, 0, buf, len);
    uint32_t decodeTime = millis() - t0;

    g_frameCount++;
    uint32_t now = millis();
    if (g_lastFrameMs > 0) {
        uint32_t delta = now - g_lastFrameMs;
        if (delta > 0) g_measuredFps = 1000.0f / delta;
    }
    g_lastFrameMs = now;

    Serial.printf("[FRAME #%d] Size: %d B | Decode: %d ms | Throughput: %.1f FPS | Free Heap: %d B\n",
                  g_frameCount, len, decodeTime, g_measuredFps, esp_get_free_heap_size());
}

// ============================================================================
// NETWORK HANDLERS (HTTP Map Server + Port 8080 JSON HUD Socket)
// ============================================================================

void processHudSocket() {
    // 1. Terima client baru jika ada (selalu timpa client lama agar tidak freeze!)
    if (hudServer.hasClient()) {
        WiFiClient newClient = hudServer.available();
        if (newClient) {
            if (g_hudClient && g_hudClient.connected()) {
                g_hudClient.stop();
            }
            g_hudClient = newClient;
            g_hudClient.setNoDelay(true);
            g_hudClient.setTimeout(100);
            Serial.printf("\n>>> [HUD SOCKET] HP BERHASIL TERHUBUNG DARI %s:8080! <<<\n",
                          g_hudClient.remoteIP().toString().c_str());
            tft.fillScreen(COLOR_BG_NAVY);
            g_idleStaticDrawn = false;
            g_mapModeActive = false;
        }
    }

    // 2. Baca data stream JSON dari HP
    if (g_hudClient && g_hudClient.connected()) {
        while (g_hudClient.available()) {
            String line = g_hudClient.readStringUntil('\n');
            line.trim();
            if (line.isEmpty()) continue;

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, line);
            if (!err) {
                const char* type = doc["t"] | "";
                if (strcmp(type, "hud") == 0) {
                    g_hud.speed        = doc["spd"] | 0;
                    g_hud.heading      = doc["hdg"] | 0;
                    g_hud.gpsStatus    = doc["gps"] | 0;
                    g_hud.turnCode     = doc["trn"] | 255;
                    g_hud.distMeters   = doc["dst"] | 0;
                    g_hud.idleMode     = doc["idle"] | 0;
                    g_hud.hasNewPacket = true;
                    g_hud.lastPacketMs = millis();
                    g_mapModeActive    = false;
                } else if (strcmp(type, "idle") == 0) {
                    g_hud.idleMode     = doc["id"] | 0;
                    g_hud.turnCode     = 255;
                    g_hud.hasNewPacket = true;
                    g_idleStaticDrawn  = false;
                    tft.fillScreen(COLOR_BG_NAVY);
                } else if (strcmp(type, "pwr") == 0) {
                    g_hud.isPowered    = doc["on"] | true;
                } else if (strcmp(type, "cfg_wifi") == 0) {
                    // Dynamic Hotspot Configuration from Companion App
                    const char* newSsid = doc["ssid"] | "";
                    const char* newPass = doc["pass"] | "";
                    if (strlen(newSsid) > 0) {
                        g_activeSsid = newSsid;
                        g_activePass = newPass;
                        g_prefs.putString("ssid", g_activeSsid);
                        g_prefs.putString("pass", g_activePass);
                        Serial.printf("[WIFI NVS] Menyimpan Hotspot Baru: %s\n", newSsid);
                        WiFi.disconnect();
                        WiFi.begin(g_activeSsid.c_str(), g_activePass.c_str());
                        g_idleStaticDrawn = false;
                    }
                }
            }
        }
    }
}

void processHttpMapServer() {
    WiFiClient client = tcpMapServer.available();
    if (!client) return;
    client.setTimeout(2000);

    String reqLine = client.readStringUntil('\r');
    if (client.read() != '\n') { client.stop(); return; }

    if (reqLine.startsWith("OPTIONS")) {
        client.print("HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n");
        client.stop();
        return;
    }

    bool isPost = reqLine.startsWith("POST");
    size_t contentLength = 0;

    while (client.connected()) {
        String line = client.readStringUntil('\r');
        if (client.read() != '\n') break;
        if (line.length() == 0) break;
        if (line.startsWith("Content-Length: ") || line.startsWith("content-length: ")) {
            contentLength = line.substring(16).toInt();
        }
    }

    if (isPost && contentLength > 0 && contentLength <= MAX_JPG_BUFFER) {
        size_t bytesRead = 0;
        uint32_t lastByteTime = millis();
        while (bytesRead < contentLength && (millis() - lastByteTime < 1000)) {
            if (client.available()) {
                size_t n = client.readBytes(g_rxBuffer + bytesRead, contentLength - bytesRead);
                bytesRead += n;
                lastByteTime = millis();
            } else {
                delay(1);
            }
        }

        if (bytesRead == contentLength) {
            renderMapFromBuffer(g_rxBuffer, bytesRead);
            client.print("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
        } else {
            client.print("HTTP/1.1 400 Bad Request\r\nContent-Length: 10\r\n\r\nINCOMPLETE");
        }
    } else {
        char info[128];
        snprintf(info, sizeof(info), "Spidey-Nav Dual-Engine Ready. Frames: %d\n", g_frameCount);
        client.printf("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s", strlen(info), info);
    }
    client.stop();
}

void processSerialStream() {
    while (Serial.available() >= 4) {
        if (Serial.peek() == 0xAA) {
            Serial.read();
            if (Serial.read() == 0x55) {
                uint8_t lenHi = Serial.read();
                uint8_t lenLo = Serial.read();
                size_t pktLen = ((size_t)lenHi << 8) | lenLo;

                if (pktLen > 0 && pktLen <= MAX_JPG_BUFFER) {
                    size_t bytesRead = 0;
                    uint32_t lastByteTime = millis();
                    while (bytesRead < pktLen && (millis() - lastByteTime < 500)) {
                        if (Serial.available()) {
                            size_t n = Serial.readBytes((char*)(g_rxBuffer + bytesRead), pktLen - bytesRead);
                            bytesRead += n;
                            lastByteTime = millis();
                        }
                    }
                    if (bytesRead == pktLen) {
                        renderMapFromBuffer(g_rxBuffer, bytesRead);
                        Serial.printf("ACK_FRAME_%d\n", g_frameCount);
                    }
                }
            }
        } else {
            Serial.read();
        }
    }
}

// ============================================================================
// MAIN SETUP & RUNTIME LOOP
// ============================================================================

void setup() {
    pinMode(HW_LED_SYS, OUTPUT);
    digitalWrite(HW_LED_SYS, HIGH);

    btnBenchmark.begin();
    btnOverlay.begin();
    btnStress.begin();

    Serial.begin(115200);
    Serial.setTimeout(20);

    tft.init();
    tft.setRotation(g_rotation); // Default 1: Landscape (320x240)
    tft.fillScreen(COLOR_BG_NAVY);

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    // Initial Welcome Splash
    renderMapFromBuffer(TEST_MAP_JPG, TEST_MAP_JPG_LEN);

    // Inisialisasi NVS Storage untuk SSID & Password Hotspot
    g_prefs.begin("spidey_net", false);
    g_activeSsid = g_prefs.getString("ssid", "logolo");
    g_activePass = g_prefs.getString("pass", "rpl11rpl");

    // Scan WiFi untuk mendiagnosa parameter sinyal & channel target secara akurat
    Serial.println("\n[WI-FI SCAN] Memindai jaringan sekitar...");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    int nNetworks = WiFi.scanNetworks();
    Serial.printf("[WI-FI SCAN] Ditemukan %d jaringan:\n", nNetworks);
    int targetChannel = 1;
    bool foundTarget = false;
    for (int i = 0; i < nNetworks; i++) {
        Serial.printf("  -> %s | Ch: %d | RSSI: %d dBm | Auth: %d\n",
                      WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i), (int)WiFi.encryptionType(i));
        if (WiFi.SSID(i) == g_activeSsid) {
            targetChannel = WiFi.channel(i);
            foundTarget = true;
        }
    }
    if (!foundTarget) {
        Serial.printf("[WI-FI SCAN WARNING] SSID '%s' tidak terdeteksi dalam jangkauan!\n", g_activeSsid.c_str());
    }

    // Wi-Fi Setup: Mode STA murni, tanpa sleep, daya RF maksimal
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.setAutoReconnect(true);

    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
        if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
            Serial.printf("\n>>> [WI-FI TERHUBUNG] IP Lokal: %s <<<\n", WiFi.localIP().toString().c_str());
            Serial.printf(">>> HP & Laptop dapat kirim data ke: %s:8080 <<<\n", WiFi.localIP().toString().c_str());
            g_idleStaticDrawn = false;
        } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
            Serial.printf("[WI-FI STA] Gagal konek (reason: %d)\n", info.wifi_sta_disconnected.reason);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Serial.println("\n>>> [SOFTAP] HP TERHUBUNG KE SpideyNav-AP! <<<");
            g_idleStaticDrawn = false;
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Serial.println("\n>>> [SOFTAP] HP TERPUTUS DARI SpideyNav-AP <<<");
            g_idleStaticDrawn = false;
        }
    });

    Serial.printf("[WI-FI STA] Menyambungkan ke '%s' (Pass: '%s')...\n",
                  g_activeSsid.c_str(), g_activePass.c_str());
    WiFi.begin(g_activeSsid.c_str(), g_activePass.c_str());

    // Beri waktu hingga 15 detik agar jabat tangan WPA2 & DHCP IP selesai sempurna
    uint32_t tConnStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - tConnStart < 15000)) {
        delay(400);
        Serial.print(".");
    }
    Serial.println();

    const char* ap_ssid = "SpideyNav-AP";
    const char* ap_pass = "12345678";

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WI-FI] Sukses terhubung ke Hotspot logolo! IP diperoleh.");
    } else {
        Serial.println("[WI-FI] Timeout 15s. Mengaktifkan mode Dual AP_STA sebagai cadangan...");
        WiFi.mode(WIFI_AP_STA);
        WiFi.softAP(ap_ssid, ap_pass);
    }

    tcpMapServer.begin();
    hudServer.begin();

    Serial.println("\n========================================================");
    Serial.println(" SPIDEY-NAV : COCKPIT HUD & DUAL-CHANNEL MAP RECEIVER");
    Serial.println("========================================================");
    Serial.printf("[WI-FI TARGET] %s (Password: %s)\n", g_activeSsid.c_str(), g_activePass.c_str());
    Serial.printf("[WI-FI SOFTAP] SSID: %s | Pass: %s\n", ap_ssid, ap_pass);
    Serial.printf("[COCKPIT HUD ] Port 8080 (Companion Socket OPEN)\n");
    Serial.printf("[HTTP SERVER ] Port 80   (http://192.168.4.1/map)\n");
    Serial.println("--------------------------------------------------------");

    digitalWrite(HW_LED_SYS, LOW);
}

void loop() {
    // 1. Pantau status koneksi Wi-Fi STA (Hanya rekoneksi jika TIDAK ada HP yang tersambung ke SoftAP)
    static uint32_t s_lastWifiCheck = 0;
    if (millis() - s_lastWifiCheck > 12000) {
        s_lastWifiCheck = millis();
        if (WiFi.status() != WL_CONNECTED && WiFi.softAPgetStationNum() == 0 && !g_hudClient.connected()) {
            Serial.printf("[WI-FI STA] Mencoba rekoneksi ke %s...\n", g_activeSsid.c_str());
            WiFi.disconnect();
            WiFi.begin(g_activeSsid.c_str(), g_activePass.c_str());
        }
    }

    // 2. Terima packet dari Companion App via Socket 8080
    processHudSocket();

    // 3. Terima stream JPEG via Wi-Fi Port 80
    processHttpMapServer();

    // 4. Terima stream JPEG via USB CDC Serial
    processSerialStream();

    // 5. Engine Render Layar (Prioritas: Map aktif > Navigasi Aktif > Standby Idle)
    uint32_t now = millis();

    if (g_mapModeActive) {
        if (now - g_lastMapFrameMs > 3000) {
            g_mapModeActive = false;
            g_idleStaticDrawn = false;
            tft.fillScreen(COLOR_BG_NAVY);
        }
    } else {
        if (g_hud.turnCode != 255) {
            // Mode Navigasi Belokan Aktif
            renderActiveNavHUD();
        } else {
            // Mode Standby Idle
            if (now - g_lastIdleTickMs > 45) { // ~22 FPS refresh
                g_lastIdleTickMs = now;
                float t = (now % 10000) / 1000.0f;
                renderIdleScreen(g_hud.idleMode, t);
            }
        }
    }

    // 6. Tombol Fisik
    // Tombol BOOT (GPIO 0): Tekan untuk ganti rotasi layar (Landscape <-> Portrait)
    if (btnBenchmark.wasPressed()) {
        g_rotation = (g_rotation == 1) ? 0 : (g_rotation == 0 ? 3 : 1);
        tft.setRotation(g_rotation);
        g_idleStaticDrawn = false;
        tft.fillScreen(COLOR_BG_NAVY);
        Serial.printf("[BTN GPIO 0] Ganti Rotasi Layar ke Mode %d (%dx%d)\n",
                      g_rotation, tft.width(), tft.height());
    }

    // Tombol ACTION (GPIO 1): Invert Display (Mode Malam/Siang)
    if (btnOverlay.wasPressed()) {
        static bool s_invert = false;
        s_invert = !s_invert;
        tft.invertDisplay(s_invert);
        Serial.printf("[BTN GPIO 1] Invert Display: %s\n", s_invert ? "ON" : "OFF");
    }
}
