/*
 * config.h - Configuration for ESP32 Dashboard
 *
 * Sunton ESP32-8048S070
 * 7" 800x480 RGB TFT + GT911 capacitive touch
 * ESP32-S3 with 8MB PSRAM
 *
 * Edit these values to match your network and API server.
 */

#ifndef CONFIG_H
#define CONFIG_H

// ============================================================================
// WiFi Configuration
// ============================================================================
// Set your WiFi credentials here before compiling.
#define WIFI_SSID       "YourWiFiSSID"
#define WIFI_PASSWORD   "YourWiFiPassword"

// How long to wait for WiFi connection before retrying (milliseconds)
#define WIFI_TIMEOUT_MS 10000

// Maximum number of reconnection attempts before entering fallback mode
#define WIFI_MAX_RETRIES 5

// ============================================================================
// Dashboard API Configuration
// ============================================================================
// The dashboard API server running on the NixOS desktop
#define API_HOST        "192.168.1.33"
#define API_PORT        8080

// API endpoint paths
#define API_WORKSPACES  "/api/workspaces"
#define API_SYSTEM      "/api/system"
#define API_VOLUME      "/api/volume"
#define API_SWITCH_WORKSPACE "/api/workspace/switch"
#define API_VOLUME_UP   "/api/volume/up"
#define API_VOLUME_DOWN "/api/volume/down"
#define API_VOLUME_MUTE "/api/volume/mute"

// Polling interval (milliseconds) - fetch dashboard data every 2 seconds
#define API_POLL_INTERVAL_MS 2000

// HTTP request timeout (milliseconds)
#define HTTP_TIMEOUT_MS 3000

// ============================================================================
// Display & Touch Configuration
// ============================================================================
// Display dimensions
#define DISPLAY_WIDTH   800
#define DISPLAY_HEIGHT  480

// Backlight pin
#define TFT_BL          2

// Touchscreen pins (GT911 on I2C)
#define TOUCH_SDA       19
#define TOUCH_SCL       20
#define TOUCH_INT       -1  // Not connected on this board
#define TOUCH_RST       38

// RGB panel pin definitions (parallel RGB-565 interface)
// Data bus: 16 pins (5 red + 6 green + 5 blue)
#define TFT_DE          41
#define TFT_VSYNC       40
#define TFT_HSYNC       39
#define TFT_PCLK        42
// Red (5 bits: R0-R4)
#define TFT_R0          14
#define TFT_R1          21
#define TFT_R2          47
#define TFT_R3          48
#define TFT_R4          45
// Green (6 bits: G0-G5)
#define TFT_G0          9
#define TFT_G1          46
#define TFT_G2          3
#define TFT_G3          8
#define TFT_G4          16
#define TFT_G5          1
// Blue (5 bits: B0-B4)
#define TFT_B0          15
#define TFT_B1          7
#define TFT_B2          6
#define TFT_B3          5
#define TFT_B4          4

// ============================================================================
// UI Configuration
// ============================================================================
#define NUMBER_OF_WORKSPACES 8
#define WORKSPACE_COLS       4
#define WORKSPACE_ROWS       2

// LVGL display buffer size (rows per buffer)
// With 2 buffers of screenWidth * 40, total = 800 * 40 * 2 * 2 = 128KB
// This avoids excessive PSRAM usage while keeping rendering smooth
// Buffer rows for LVGL partial rendering
// Higher = smoother but more RAM. 40 works without PSRAM, 80 is smoother
#define LVGL_BUF_ROWS   40

#endif // CONFIG_H
