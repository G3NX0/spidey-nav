#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "test_map_jpg.h"

// ============================================================================
// SPIDEY-NAV : COCKPIT HUD & DUAL-STREAM RECEIVER (WI-FI + SERIAL)
// ============================================================================
// Platform: ESP32-C3 Super Mini + 2.4" SPI TFT (ILI9341 320x240 Landscape)
// Protocol 1: TCP Port 8080 - High-Speed JSON Stream from Flutter Companion
// Protocol 2: HTTP Port 80   - Binary JPEG /map Stream (Web / Python Testbench)
// Protocol 3: USB-CDC Serial - High-Speed Binary Stream (0xAA 0x55 [H] [L] [JPEG])
// ============================================================================

TFT_eSPI tft = TFT_eSPI();
WiFiServer tcpMapServer(80);
WiFiServer hudServer(8080);
WiFiClient g_hudClient;

// --- 16-bit 565 Colors (Strict 3-Tone Spidey Palette) ---
constexpr uint16_t COLOR_BG_NAVY     = 0x08A5; // #0D1527 Deep Obsidian Midnight
constexpr uint16_t COLOR_SPIDEY_RED  = 0xE104; // #E62429 Crimson Red
constexpr uint16_t COLOR_TEXT_WHITE  = 0xFFFF; // #F0F4F8 Pure Crisp White
constexpr uint16_t COLOR_EMERALD     = 0x07E0; // #00E676 GPS Locked
constexpr uint16_t COLOR_AMBER       = 0xFD20; // #FFAB00 GPS Searching
constexpr uint16_t COLOR_CARD_BORDER = 0x2128; // Subtle glass outline

// --- Static Buffer untuk JPEG Masuk ---
constexpr size_t MAX_JPG_BUFFER = 32768;
static uint8_t   g_rxBuffer[MAX_JPG_BUFFER];

// --- Hardware Input Pins ---
constexpr uint8_t HW_BTN_MODE   = 0; // GPIO 0 (Boot button Super Mini)
constexpr uint8_t HW_BTN_ACTION = 1; // GPIO 1
constexpr uint8_t HW_BTN_STRESS = 4; // GPIO 4
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
static uint32_t g_lastIdleTickMs = 0;
static uint32_t g_lastMapFrameMs = 0;
static bool     g_mapModeActive  = false;
static float    g_measuredFps    = 0.0f;
static uint32_t g_frameCount     = 0;
static uint32_t g_lastFrameMs    = 0;

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
// HUD VECTOR RENDERING ENGINE (Flicker-Free, No Full Screen Refresh)
// ============================================================================

void drawTurnArrow(int trn, int cx, int cy) {
    // Clear arrow bounding box (100x100)
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
    static int s_lastTrn = -1;
    static int s_lastDist = -1;
    static int s_lastSpd = -1;
    static int s_lastGps = -1;

    // Header Card: Jarak manuver belokan
    if (g_hud.distMeters != s_lastDist) {
        s_lastDist = g_hud.distMeters;
        tft.fillRoundRect(12, 10, 296, 44, 12, 0x1105);
        tft.drawRoundRect(12, 10, 296, 44, 12, COLOR_SPIDEY_RED);

        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(COLOR_TEXT_WHITE, 0x1105);
        tft.setTextSize(1);
        char distStr[32];
        if (s_lastDist < 1000) {
            snprintf(distStr, sizeof(distStr), "%d METER MENUJU BELOKAN", s_lastDist);
        } else {
            snprintf(distStr, sizeof(distStr), "%.1f KM MENUJU BELOKAN", s_lastDist / 1000.0f);
        }
        tft.drawString(distStr, 160, 32, 4);
    }

    // Center: Panah Belokan Spidey
    if (g_hud.turnCode != s_lastTrn) {
        s_lastTrn = g_hud.turnCode;
        drawTurnArrow(s_lastTrn, 160, 122);
    }

    // Bottom Speedometer & GPS Status Bar
    if (g_hud.speed != s_lastSpd || g_hud.gpsStatus != s_lastGps) {
        s_lastSpd = g_hud.speed;
        s_lastGps = g_hud.gpsStatus;

        // Speedometer Box (Kiri Bawah)
        tft.fillRoundRect(12, 182, 140, 48, 10, 0x1105);
        tft.drawRoundRect(12, 182, 140, 48, 10, COLOR_CARD_BORDER);

        tft.setTextDatum(ML_DATUM);
        tft.setTextColor(COLOR_TEXT_WHITE, 0x1105);
        char spdStr[16];
        snprintf(spdStr, sizeof(spdStr), "%d", s_lastSpd);
        tft.drawString(spdStr, 24, 206, 6); // Large font
        tft.drawString("KM/H", 94, 208, 2);

        // GPS & Status Pill (Kanan Bawah)
        tft.fillRoundRect(168, 182, 140, 48, 10, 0x1105);
        tft.drawRoundRect(168, 182, 140, 48, 10, COLOR_CARD_BORDER);

        tft.setTextDatum(MC_DATUM);
        if (s_lastGps == 1) {
            tft.fillCircle(186, 206, 6, COLOR_EMERALD);
            tft.setTextColor(COLOR_EMERALD, 0x1105);
            tft.drawString("GPS LOCKED", 242, 206, 2);
        } else {
            tft.fillCircle(186, 206, 6, COLOR_AMBER);
            tft.setTextColor(COLOR_AMBER, 0x1105);
            tft.drawString("SEARCHING...", 242, 206, 2);
        }
    }
}

// ============================================================================
// STANDBY IDLE ANIMATIONS (Spider Eyes, Web Radar, Pulse Sonar, Speedo)
// ============================================================================

void renderIdleScreen(int mode, float t) {
    constexpr int cx = 160;
    constexpr int cy = 110;

    switch (mode) {
        case 0: { // Spider Eyes Blinking
            float blink = (sinf(t * 6.28f) * 0.5f + 0.5f);
            int eyeH = 16 + (int)(blink * 14.0f);

            tft.fillRect(cx - 65, cy - 35, 130, 70, COLOR_BG_NAVY);

            // Left Eye
            tft.fillTriangle(cx - 50, cy - eyeH, cx - 10, cy, cx - 42, cy + (int)(eyeH * 0.8f), COLOR_TEXT_WHITE);
            tft.drawTriangle(cx - 50, cy - eyeH, cx - 10, cy, cx - 42, cy + (int)(eyeH * 0.8f), COLOR_SPIDEY_RED);

            // Right Eye
            tft.fillTriangle(cx + 50, cy - eyeH, cx + 10, cy, cx + 42, cy + (int)(eyeH * 0.8f), COLOR_TEXT_WHITE);
            tft.drawTriangle(cx + 50, cy - eyeH, cx + 10, cy, cx + 42, cy + (int)(eyeH * 0.8f), COLOR_SPIDEY_RED);
            break;
        }
        case 1: { // Expanding Spider Web Radar
            static int lastR = 0;
            int r = (int)(fmodf(t * 30.0f, 40.0f)) + 12;

            if (abs(r - lastR) > 3) {
                tft.fillCircle(cx, cy, 60, COLOR_BG_NAVY);
                // 8 radial web axes
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
        default: { // Digital HUD Clock / Speedo Standby
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT_WHITE, COLOR_BG_NAVY);
            tft.drawString("SPIDEY COCKPIT", cx, cy - 25, 4);
            tft.setTextColor(COLOR_SPIDEY_RED, COLOR_BG_NAVY);
            tft.drawString("STANDBY", cx, cy + 12, 4);
            break;
        }
    }

    // Status Footer di Layar Standby
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, COLOR_BG_NAVY);
    tft.drawString("MENUNGGU RUTE DARI HP...", cx, 215, 2);
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
    if (hudServer.hasClient()) {
        if (!g_hudClient || !g_hudClient.connected()) {
            g_hudClient = hudServer.available();
            g_hudClient.setNoDelay(true);
            g_hudClient.setTimeout(200);
            Serial.printf("[HUD SOCKET] Companion HP Terhubung dari %s:8080!\n",
                          g_hudClient.remoteIP().toString().c_str());
            tft.fillScreen(COLOR_BG_NAVY);
            g_mapModeActive = false;
        }
    }

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
                    tft.fillScreen(COLOR_BG_NAVY);
                } else if (strcmp(type, "pwr") == 0) {
                    g_hud.isPowered    = doc["on"] | true;
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
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(COLOR_BG_NAVY);

    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    // Initial Welcome Splash
    renderMapFromBuffer(TEST_MAP_JPG, TEST_MAP_JPG_LEN);

    // Wi-Fi SoftAP Setup (Mitigasi Brownout 8.5 dBm)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);

    const char* sta_ssid = "logolo";
    const char* sta_pass = "rpl11rpl";
    WiFi.begin(sta_ssid, sta_pass);

    const char* ap_ssid = "SpideyNav-AP";
    const char* ap_pass = "12345678";
    WiFi.softAP(ap_ssid, ap_pass);

    tcpMapServer.begin();
    hudServer.begin();

    Serial.println("\n========================================================");
    Serial.println(" SPIDEY-NAV : COCKPIT HUD & DUAL-CHANNEL MAP RECEIVER");
    Serial.println("========================================================");
    Serial.printf("[WI-FI SOFTAP] SSID: %s | Pass: %s\n", ap_ssid, ap_pass);
    Serial.printf("[COCKPIT HUD ] Port 8080 (Companion Socket)\n");
    Serial.printf("[HTTP SERVER ] Port 80   (http://%s/map)\n", WiFi.softAPIP().toString().c_str());
    Serial.println("--------------------------------------------------------");

    digitalWrite(HW_LED_SYS, LOW);
}

void loop() {
    // 1. Terima packet dari Companion App via Socket 8080
    processHudSocket();

    // 2. Terima stream JPEG via Wi-Fi Port 80
    processHttpMapServer();

    // 3. Terima stream JPEG via USB CDC Serial
    processSerialStream();

    // 4. Engine Render Layar (Prioritas: Map aktif > Navigasi Aktif > Standby Idle)
    uint32_t now = millis();

    if (g_mapModeActive) {
        // Jika stream map aktif, tunggu hingga tidak ada map baru selama 3 detik
        if (now - g_lastMapFrameMs > 3000) {
            g_mapModeActive = false;
            tft.fillScreen(COLOR_BG_NAVY);
        }
    } else {
        if (g_hud.turnCode != 255) {
            // Mode Navigasi Belokan Aktif
            renderActiveNavHUD();
        } else {
            // Mode Standby Idle
            if (now - g_lastIdleTickMs > 45) { // ~22 FPS refresh untuk animasi
                g_lastIdleTickMs = now;
                float t = (now % 10000) / 1000.0f;
                renderIdleScreen(g_hud.idleMode, t);
            }
        }
    }

    // 5. Tombol Fisik
    if (btnBenchmark.wasPressed()) {
        static bool s_invert = false;
        s_invert = !s_invert;
        tft.invertDisplay(s_invert);
        Serial.printf("[BTN GPIO 0] Invert Display: %s\n", s_invert ? "ON" : "OFF");
    }
}
