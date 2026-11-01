#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include "test_map_jpg.h"

// ============================================================================
// SPIDEY-NAV : DUAL-CHANNEL MAP STREAMING RECEIVER (WI-FI AP + USB-CDC)
// ============================================================================
// Platform: ESP32-C3 Super Mini + 2.4" SPI TFT (ILI9341 320x240 Landscape)
// Channel 1: Wi-Fi SoftAP (SSID: "SpideyNav-AP", Pass: "12345678") -> HTTP POST /map
// Channel 2: USB-CDC Serial High-Speed Binary Stream (0xAA 0x55 [len_hi] [len_lo] [JPEG])
// ============================================================================

TFT_eSPI tft = TFT_eSPI();
WiFiServer tcpServer(80);

// --- Static Buffer untuk JPEG Masuk (Zero Dynamic Heap Allocation) ---
constexpr size_t MAX_JPG_BUFFER = 32768; // 32 KB cukup untuk JPEG 320x240 quality 85%
static uint8_t   g_rxBuffer[MAX_JPG_BUFFER];

// --- Hardware Input Pins ---
constexpr uint8_t HW_BTN_MODE   = 0;  // Tombol 1 -> GPIO 0 (Re-render Demo)
constexpr uint8_t HW_BTN_ACTION = 1;  // Tombol 2 -> GPIO 1 (Toggle HUD Overlay)
constexpr uint8_t HW_BTN_STRESS = 4;  // Tombol 3 -> GPIO 4 (10x Stress Test)
constexpr uint8_t HW_LED_SYS    = 8;  // Onboard LED (Active LOW)

// --- TJpg_Decoder Callback: Direct MCU Stream to SPI ---
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return 0;
    tft.pushImage(x, y, w, h, bitmap);
    return 1;
}

// --- Button Debouncer ---
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

// Status Global
static bool     g_hasLiveStream = false;
static bool     g_showOverlay   = false; // Default FALSE: Cegah tabrakan overlay internal dengan stream Flutter
static uint16_t g_simSpeed      = 0;
static uint32_t g_frameCount    = 0;
static uint32_t g_lastFrameMs   = 0;
static float    g_measuredFps   = 0.0f;

// Mini Info Badge: Hanya status bar tipis di tepi bawah kanan jika diaktifkan user
void drawMiniInfoBadge() {
    tft.fillRoundRect(160, 215, 150, 20, 4, 0x10A2);
    tft.drawRoundRect(160, 215, 150, 20, 4, 0x07E0);
    tft.setTextColor(0x07E0, 0x10A2);
    tft.setTextSize(1);
    char netStr[32];
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(netStr, sizeof(netStr), "%s | %.1fFPS", WiFi.localIP().toString().c_str(), g_measuredFps);
    } else {
        snprintf(netStr, sizeof(netStr), "192.168.4.1 | %.1fFPS", g_measuredFps);
    }
    tft.drawString(netStr, 166, 221);
}

void renderMapFromBuffer(const uint8_t* buf, size_t len, bool allowOverlay = false) {
    uint32_t t0 = millis();
    TJpgDec.drawJpg(0, 0, buf, len);
    uint32_t decodeTime = millis() - t0;

    g_frameCount++;
    uint32_t now = millis();
    if (g_lastFrameMs > 0) {
        uint32_t delta = now - g_lastFrameMs;
        if (delta > 0) {
            g_measuredFps = 1000.0f / delta;
        }
    }
    g_lastFrameMs = now;

    // Hanya tampilkan badge jika tombol GPIO diaktifkan secara eksplisit
    if (g_showOverlay && allowOverlay) {
        drawMiniInfoBadge();
    }

    Serial.printf("[FRAME #%d] Size: %d B | Decode: %d ms | Throughput: %.1f FPS | Free Heap: %d B\n",
                  g_frameCount, len, decodeTime, g_measuredFps, esp_get_free_heap_size());
}

// --- Handler 1: Stream HTTP Wi-Fi (SoftAP) ---
void processClientRequests() {
    WiFiClient client = tcpServer.available();
    if (!client) return;

    client.setTimeout(2000);

    String reqLine = client.readStringUntil('\r');
    if (client.read() != '\n') { client.stop(); return; }

    // Tangani preflight OPTIONS dari browser (CORS support untuk Flutter Web)
    if (reqLine.startsWith("OPTIONS")) {
        client.print("HTTP/1.1 204 No Content\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\nAccess-Control-Allow-Headers: *\r\nAccess-Control-Max-Age: 86400\r\nConnection: close\r\n\r\n");
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
            renderMapFromBuffer(g_rxBuffer, bytesRead, false);
            client.print("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Type: text/plain\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
        } else {
            Serial.printf("[ERROR] Incomplete Wi-Fi payload: %d / %d bytes\n", bytesRead, contentLength);
            client.print("HTTP/1.1 400 Bad Request\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: 10\r\n\r\nINCOMPLETE");
        }
    } else {
        char info[128];
        snprintf(info, sizeof(info), "Spidey-Nav Dual-Channel Receiver Ready. Frames: %d\n", g_frameCount);
        client.printf("HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s", strlen(info), info);
    }

    client.stop();
}

// --- Handler 2: Stream Binary USB-CDC High-Speed (0xAA 0x55 [H] [L] [Data]) ---
void processSerialStream() {
    while (Serial.available() >= 4) {
        if (Serial.peek() == 0xAA) {
            Serial.read(); // pop 0xAA
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
                            lastByteTime = millis(); // Reset timer saat ada data masuk
                        }
                    }
                    if (bytesRead == pktLen) {
                        renderMapFromBuffer(g_rxBuffer, bytesRead);
                        Serial.printf("ACK_FRAME_%d\n", g_frameCount);
                    } else {
                        Serial.printf("[ERROR] Serial incomplete: %d/%d B\n", bytesRead, pktLen);
                    }
                }
            }
        } else {
            Serial.read(); // Discard noise
        }
    }
}

void setup() {
    pinMode(HW_LED_SYS, OUTPUT);
    digitalWrite(HW_LED_SYS, HIGH);

    btnBenchmark.begin();
    btnOverlay.begin();
    btnStress.begin();

    Serial.begin(115200);
    Serial.setTimeout(20); // Fast timeout for responsive block streaming
    uint32_t startWait = millis();
    while (!Serial && (millis() - startWait < 1500)) delay(10);

    Serial.println("\n========================================================");
    Serial.println(" SPIDEY-NAV : DUAL-CHANNEL MAP RECEIVER (WI-FI + SERIAL)");
    Serial.println("========================================================");

    // 1. Inisialisasi Layar TFT (27 MHz SPI)
    tft.init();
    tft.setRotation(1); // 320x240 Landscape
    tft.fillScreen(TFT_BLACK);

    // 2. Inisialisasi TJpg_Decoder
    TJpgDec.setJpgScale(1);
    TJpgDec.setSwapBytes(true);
    TJpgDec.setCallback(tft_output);

    // Render demo awal dari Flash
    renderMapFromBuffer(TEST_MAP_JPG, TEST_MAP_JPG_LEN);

    // 3. Inisialisasi Wi-Fi Dual-Mode (STA + AP) dengan Mitigasi Brownout (Tx Power 8.5dBm)
    WiFi.mode(WIFI_AP_STA);
    WiFi.setTxPower(WIFI_POWER_8_5dBm);
    
    // Sambungkan ke Wi-Fi logolo
    const char* sta_ssid = "logolo";
    const char* sta_pass = "rpl11rpl";
    WiFi.begin(sta_ssid, sta_pass);

    // SoftAP cadangan tetap dipancarkan
    const char* ap_ssid = "SpideyNav-AP";
    const char* ap_pass = "12345678";
    WiFi.softAP(ap_ssid, ap_pass);

    Serial.printf("[WIFI] Menyambungkan ke '%s'...", sta_ssid);
    uint32_t staStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - staStart < 8000)) {
        delay(250);
        Serial.print(".");
    }
    Serial.println();

    tcpServer.begin();

    Serial.println("--------------------------------------------------------");
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("[WIFI TERHUBUNG] Jaringan: %s\n", sta_ssid);
        Serial.printf("[IP LOKAL ANDA ] http://%s/map\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("[WIFI STA TIMEOUT] Gagal konek ke '%s'. Gunakan SoftAP cadangan.\n", sta_ssid);
    }
    Serial.printf("[SOFTAP CADANGAN] SSID: %s | http://%s/map\n", ap_ssid, WiFi.softAPIP().toString().c_str());
    Serial.println("[CHANNEL USB-CDC] Protocol: 0xAA 0x55 [H] [L] [JPEG]");
    Serial.printf("[SYSTEM FREE HEAP] %d bytes\n", esp_get_free_heap_size());
    Serial.println("--------------------------------------------------------");

    digitalWrite(HW_LED_SYS, LOW);
}

uint32_t g_lastSpeedTick = 0;

void loop() {
    // Saluran 1: Terima stream HTTP dari Wi-Fi (Mobile App)
    processClientRequests();

    // Saluran 2: Terima stream biner dari USB Serial (PC Python Testbench)
    processSerialStream();

    // =========================================================================
    // KONTROL TOMBOL FISIK (NON-DESTRUCTIVE - TIDAK MERESET MAP STREAM)
    // =========================================================================
    // PERINGATAN PINOUT HARDWARE:
    // - GPIO 0: Tombol BOOT Super Mini -> Toggle Mini Info Badge (IP + FPS bar)
    // - GPIO 1: Tombol Action -> Toggle Mode Malam/Siang (Invert Display)
    // - GPIO 7: PERINGATAN KERAS! GPIO 7 adalah TFT_MOSI (Jalur Data SPI Layar).
    //           DILARANG menghubungkan saklar/pushbutton ke GPIO 7!
    // =========================================================================
    if (btnBenchmark.wasPressed()) {
        g_showOverlay = !g_showOverlay;
        if (g_showOverlay) {
            drawMiniInfoBadge();
        }
        Serial.printf("[BTN GPIO 0] Toggle Info Badge: %s\n", g_showOverlay ? "AKTIF" : "NONAKTIF");
    }
    if (btnOverlay.wasPressed()) {
        static bool s_inverted = false;
        s_inverted = !s_inverted;
        tft.invertDisplay(s_inverted);
        Serial.printf("[BTN GPIO 1] Toggle Mode Malam (Invert): %s\n", s_inverted ? "AKTIF" : "NORMAL");
    }
    if (btnStress.wasPressed()) {
        Serial.printf("[BTN GPIO 4] System Diagnostics: Free Heap = %d B, FPS = %.1f, Total Frames = %d\n",
                      esp_get_free_heap_size(), g_measuredFps, g_frameCount);
    }
}
