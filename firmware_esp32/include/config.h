#pragma once

#include <Arduino.h>

// ============================================================================
// Hardware Pinout Definitions
// ============================================================================

// Tactile Switch Inputs (Active LOW with internal pull-up)
// Confirmed 100% safe from ESP32-C3 Strapping Pins (GPIO 2, 8, 9 avoided)
#define PIN_BTN_MODE    0
#define PIN_BTN_SELECT  10
#define PIN_BTN_ZOOM    20

// Display backlight control (Active HIGH)
#define PIN_TFT_BL      3

// ============================================================================
// UI & Display Layout Constants
// ============================================================================
#define SCREEN_ROTATION 0 // 0 = Portrait (240x320), 1 = Landscape (320x240)
#define SCREEN_WIDTH    240
#define SCREEN_HEIGHT   320

// Partial Sprite dimensions (Conserves RAM: 180x120 @ 16-bit = ~43 KB RAM)
#define SPRITE_NAV_W    180
#define SPRITE_NAV_H    120
#define SPRITE_NAV_X    ((SCREEN_WIDTH - SPRITE_NAV_W) / 2)
#define SPRITE_NAV_Y    70

// Color Palette (Retro Synthwave / Cyberpunk Cyber-Red Theme)
#define COLOR_BG        0x0821 // Deep Obsidian / Dark Slate
#define COLOR_ACCENT    0xF800 // Vibrant Spidey Red
#define COLOR_SECONDARY 0x07E0 // Bright Emerald Green
#define COLOR_TEXT_MUTED 0x7BEF // Muted Light Gray
#define COLOR_WHITE     0xFFFF // Crisp White
#define COLOR_CARD_BG   0x18C3 // Dark Panel Background

// ============================================================================
// BLE Configuration (GATT Server)
// ============================================================================
#define BLE_DEVICE_NAME        "Spidey-Nav"
#define SERVICE_UUID           "0000FFF0-0000-1000-8000-00805F9B34FB"
#define CHAR_NAV_DATA_UUID     "0000FFF1-0000-1000-8000-00805F9B34FB"

// Navigation direction codes
enum class NavAction : uint8_t {
    IDLE = 0,
    STRAIGHT = 1,
    SLIGHT_LEFT = 2,
    TURN_LEFT = 3,
    SHARP_LEFT = 4,
    SLIGHT_RIGHT = 5,
    TURN_RIGHT = 6,
    SHARP_RIGHT = 7,
    U_TURN = 8,
    ROUNDABOUT = 9,
    DESTINATION_REACHED = 10
};

// Thread-safe navigation state packet
struct NavigationData {
    NavAction action;
    uint32_t distanceMeters;
    char streetName[32];
    bool isConnected;
    bool hasNewData;
};
