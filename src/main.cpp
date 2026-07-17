/*
 * main.cpp - ESP32 Dashboard Controller
 *
 * Sunton ESP32-8048S070
 * 7" 800x480 RGB TFT + GT911 capacitive touch
 * ESP32-S3 with 8MB PSRAM
 *
 * Features:
 *   - WiFi connectivity with auto-reconnect
 *   - HTTP API polling for dashboard data
 *   - LVGL 9.x touch-enabled UI with 3 swipeable pages:
 *       Page 1: Workspace Overview (grid of 8 workspaces)
 *       Page 2: System Monitor (CPU temp, RAM, disk)
 *       Page 3: Volume Controls (up/down/mute)
 *   - Touch-interactive workspace switching and volume control
 *
 * Tech stack: Arduino Framework + LVGL 9 + LovyanGFX + GT911
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_heap_caps.h>   // for heap_caps_malloc with MALLOC_CAP_DMA

#include <lvgl.h>
#include "LGFX_Sunton_ESP32_8048S070.hpp"

#include "config.h"

// ============================================================================
// Global State
// ============================================================================

// --- Display & Touch (LovyanGFX) ---
static LGFX lcd;

// --- LVGL ---
static lv_display_t  *lvgl_disp      = nullptr;
static lv_color_t    *lvgl_draw_buf  = nullptr;
static lv_indev_t    *lvgl_indev     = nullptr;

// --- UI objects (needed from callbacks) ---
static lv_obj_t *page_container  = nullptr;   // cont with 3 pages side-by-side
static lv_obj_t *page_indicator  = nullptr;   // dot indicator at bottom

// Workspace grid labels
static lv_obj_t *ws_labels[NUMBER_OF_WORKSPACES];

// System monitor labels
static lv_obj_t *label_cpu_temp  = nullptr;
static lv_obj_t *label_ram       = nullptr;
static lv_obj_t *label_disk      = nullptr;

// Volume controls
static lv_obj_t *label_volume    = nullptr;
static lv_obj_t *btn_vol_up     = nullptr;
static lv_obj_t *btn_vol_down   = nullptr;
static lv_obj_t *btn_vol_mute   = nullptr;
static lv_obj_t *bar_volume     = nullptr;

// --- Data state ---
static int current_page = 0;          // 0, 1, or 2
static int volume_level = 0;
static bool volume_muted = false;

// --- Timing ---
static unsigned long last_poll_ms = 0;

// ============================================================================
// Forward Declarations
// ============================================================================
static void api_poll_all();
static void switch_workspace(int id);
static void switch_page(int page, bool animated);
static void volume_up();
static void volume_down();
static void volume_toggle_mute();

// ============================================================================
// Display & Touch Initialization
// ============================================================================

static void init_display() {
    Serial.println("[DISPLAY] Initializing with LovyanGFX...");

    lcd.init();
    lcd.setBrightness(200);  // ~78% brightness
    lcd.fillScreen(TFT_BLACK);

    Serial.println("[DISPLAY] Display initialized OK (white flash seen = hardware works)");

    // Check PSRAM availability
    if (psramFound()) {
        Serial.printf("[DISPLAY] PSRAM found: %d bytes total, %d bytes free\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
    } else {
        Serial.println("[DISPLAY] WARNING: No PSRAM found! Performance will suffer.");
    }
}

// No separate init_touch() needed - LovyanGFX handles GT911 internally
// via the LGFX board configuration class.

// ============================================================================
// WiFi Management
// ============================================================================

/**
 * Return a human-readable name for the WiFi status code.
 */
static const char* wifi_status_name(wl_status_t status) {
    switch (status) {
        case WL_IDLE_STATUS:      return "WL_IDLE_STATUS";
        case WL_NO_SSID_AVAIL:    return "WL_NO_SSID_AVAIL";
        case WL_SCAN_COMPLETED:   return "WL_SCAN_COMPLETED";
        case WL_CONNECTED:        return "WL_CONNECTED";
        case WL_CONNECT_FAILED:   return "WL_CONNECT_FAILED";
        case WL_CONNECTION_LOST:  return "WL_CONNECTION_LOST";
        case WL_DISCONNECTED:     return "WL_DISCONNECTED";
        default:                  return "UNKNOWN";
    }
}

static bool wifi_connect() {
    Serial.printf("[WIFI] Connecting to SSID: \"%s\" ...\n", WIFI_SSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long start = millis();
    unsigned long last_dot = start;
    while (WiFi.status() != WL_CONNECTED) {
        unsigned long now = millis();
        if (now - start > WIFI_TIMEOUT_MS) {
            wl_status_t status = WiFi.status();
            Serial.printf("\n[WIFI] Connection timeout after %d ms! Status: %d (%s)\n",
                          WIFI_TIMEOUT_MS, status, wifi_status_name(status));
            Serial.printf("[WIFI] SSID used: \"%s\". Check that the network is reachable.\n",
                          WIFI_SSID);
            return false;
        }
        // Print a dot every 500ms
        if (now - last_dot > 500) {
            last_dot = now;
            wl_status_t s = WiFi.status();
            Serial.printf("[%d|%s] ", s, wifi_status_name(s));
        }
        delay(200);
    }

    Serial.printf("\n[WIFI] Connected!\n");
    Serial.printf("[WIFI]   SSID: %s\n", WiFi.SSID().c_str());
    Serial.printf("[WIFI]   IP:   %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI]   RSSI: %d dBm\n", WiFi.RSSI());
    return true;
}

static void wifi_ensure_connected() {
    static int retries = 0;

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[WIFI] Disconnected! Reconnecting...");
        WiFi.disconnect();
        if (wifi_connect()) {
            retries = 0;
        } else {
            retries++;
            if (retries >= WIFI_MAX_RETRIES) {
                Serial.println("[WIFI] Max retries reached. Will keep trying...");
                retries = 0;
            }
        }
    }
}

// ============================================================================
// HTTP API Client
// ============================================================================

/**
 * Perform an HTTP GET request to the dashboard API.
 * @param path  API endpoint path (e.g., "/api/system")
 * @param json  Reference to a StaticJsonDocument to fill with parsed response
 * @return true if the request succeeded and JSON was parsed
 */
static bool http_get(const char *path, JsonDocument &json) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;

    String url = String("http://") + API_HOST + ":" + API_PORT + path;

    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Accept", "application/json");

    int code = http.GET();

    if (code <= 0) {
        Serial.printf("[HTTP] GET %s failed, code=%d\n", path, code);
        http.end();
        return false;
    }

    if (code != 200) {
        Serial.printf("[HTTP] GET %s returned %d\n", path, code);
        http.end();
        return false;
    }

    DeserializationError err = deserializeJson(json, http.getStream());
    http.end();

    if (err) {
        Serial.printf("[HTTP] JSON parse error for %s: %s\n", path, err.c_str());
        return false;
    }

    return true;
}

/**
 * Perform an HTTP POST to the dashboard API with an optional JSON body.
 */
static bool http_post(const char *path, JsonDocument *body = nullptr) {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClient client;
    HTTPClient http;

    String url = String("http://") + API_HOST + ":" + API_PORT + path;
    http.begin(client, url);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");

    int code;
    if (body) {
        String payload;
        serializeJson(*body, payload);
        code = http.POST(payload);
    } else {
        code = http.POST("{}");
    }

    http.end();
    return (code == 200 || code == 204);
}

// ============================================================================
// LVGL Display & Touch Callbacks
// ============================================================================

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    // Use blocking pushImage instead of pushImageDMA to avoid a race condition:
    // pushImageDMA is asynchronous, and calling lv_disp_flush_ready immediately
    // tells LVGL to reuse the buffer while DMA is still reading from it.
    // This causes display corruption / blank screen.
    // Once the display works reliably, DMA can be re-enabled with proper waits.
    lcd.pushImage(area->x1, area->y1, w, h, (uint16_t *)px_map);
    // For DMA version (faster but needs DMA wait):
    // lcd.pushImageDMA(area->x1, area->y1, w, h, (uint16_t *)px_map);
    // while (lcd.dmaBusy()) {}   // or lcd.waitDma();

    lv_disp_flush_ready(disp);

    // Debug: print first few flushes to verify pipeline
    static int flush_count = 0;
    if (flush_count < 5) {
        Serial.printf("[LVGL] Flush #%d: area=(%d,%d)-(%d,%d) size=%dx%d\n",
                      ++flush_count, area->x1, area->y1, area->x2, area->y2, w, h);
    }
}

static void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    int16_t tx = -1, ty = -1;

    if (lcd.getTouch(&tx, &ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static uint32_t lvgl_tick_cb() {
    return millis();
}

// ============================================================================
// LVGL UI Construction
// ============================================================================

/**
 * Build Page 1: Workspace Overview
 * Displays 8 workspace boxes in a 4x2 grid. Each box shows the workspace
 * name and number of open windows. Tapping a box sends a switch command.
 */
static lv_obj_t* build_page_workspaces(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(page, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_SPACE_EVENLY);
    lv_obj_set_style_pad_all(page, 8, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x1a1a2e), 0);

    // Title
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Workspaces");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(title, 4, 0);

    // Create workspace buttons in a grid
    // Since we put this title at top and then use a grid, let's use a grid container
    lv_obj_t *grid = lv_obj_create(page);
    lv_obj_set_size(grid, DISPLAY_WIDTH - 16, DISPLAY_HEIGHT - 80);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);

    // Create grid layout: 4 columns, rows auto-sized
    static lv_coord_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1),
                                    LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(grid, col_dsc, row_dsc);

    for (int i = 0; i < NUMBER_OF_WORKSPACES; i++) {
        lv_obj_t *btn = lv_btn_create(grid);
        lv_obj_set_grid_cell(btn, LV_GRID_ALIGN_STRETCH, i % WORKSPACE_COLS, 1,
                                  LV_GRID_ALIGN_STRETCH, i / WORKSPACE_COLS, 1);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x16213e), 0);
        lv_obj_set_style_shadow_width(btn, 6, 0);
        lv_obj_set_style_shadow_color(btn, lv_color_hex(0x000000), 0);
        lv_obj_set_style_shadow_opa(btn, LV_OPA_40, 0);

        // Store workspace ID in user data
        lv_obj_set_user_data(btn, (void *)(intptr_t)(i + 1));

        // Label inside button
        ws_labels[i] = lv_label_create(btn);
        lv_label_set_text_fmt(ws_labels[i], "WS %d\nLoading...", i + 1);
        lv_obj_set_style_text_color(ws_labels[i], lv_color_hex(0xe0e0e0), 0);
        lv_obj_center(ws_labels[i]);

        // Tap handler to switch workspace
        lv_obj_add_event_cb(btn, [](lv_event_t *e) {
            void *user_data = lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e));
            int ws_id = (int)(intptr_t)user_data;
            switch_workspace(ws_id);
        }, LV_EVENT_CLICKED, nullptr);
    }

    return page;
}

/**
 * Build Page 2: System Monitor
 * Shows CPU temperature, RAM usage, and disk usage with bars.
 */
static lv_obj_t* build_page_system(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x16213e), 0);

    // Title
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "System Monitor");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // --- CPU Temperature ---
    lv_obj_t *cont_cpu = lv_obj_create(page);
    lv_obj_set_size(cont_cpu, DISPLAY_WIDTH - 32, 90);
    lv_obj_set_style_border_width(cont_cpu, 0, 0);
    lv_obj_set_style_bg_color(cont_cpu, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(cont_cpu, 8, 0);

    lv_obj_t *lbl_cpu = lv_label_create(cont_cpu);
    lv_label_set_text(lbl_cpu, "CPU Temperature");
    lv_obj_set_style_text_color(lbl_cpu, lv_color_hex(0x00d2ff), 0);
    lv_obj_align(lbl_cpu, LV_ALIGN_TOP_LEFT, 12, 8);

    label_cpu_temp = lv_label_create(cont_cpu);
    lv_label_set_text(label_cpu_temp, "--\u00b0C");
    lv_obj_set_style_text_font(label_cpu_temp, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label_cpu_temp, lv_color_hex(0xffffff), 0);
    lv_obj_align(label_cpu_temp, LV_ALIGN_LEFT_MID, 12, 8);

    // --- RAM Usage ---
    lv_obj_t *cont_ram = lv_obj_create(page);
    lv_obj_set_size(cont_ram, DISPLAY_WIDTH - 32, 90);
    lv_obj_set_style_border_width(cont_ram, 0, 0);
    lv_obj_set_style_bg_color(cont_ram, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(cont_ram, 8, 0);

    lv_obj_t *lbl_ram = lv_label_create(cont_ram);
    lv_label_set_text(lbl_ram, "RAM");
    lv_obj_set_style_text_color(lbl_ram, lv_color_hex(0x00ff88), 0);
    lv_obj_align(lbl_ram, LV_ALIGN_TOP_LEFT, 12, 8);

    label_ram = lv_label_create(cont_ram);
    lv_label_set_text(label_ram, "-- GB / -- GB");
    lv_obj_set_style_text_font(label_ram, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label_ram, lv_color_hex(0xffffff), 0);
    lv_obj_align(label_ram, LV_ALIGN_LEFT_MID, 12, 8);

    // --- Disk Usage ---
    lv_obj_t *cont_disk = lv_obj_create(page);
    lv_obj_set_size(cont_disk, DISPLAY_WIDTH - 32, 90);
    lv_obj_set_style_border_width(cont_disk, 0, 0);
    lv_obj_set_style_bg_color(cont_disk, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_radius(cont_disk, 8, 0);

    lv_obj_t *lbl_disk = lv_label_create(cont_disk);
    lv_label_set_text(lbl_disk, "Disk");
    lv_obj_set_style_text_color(lbl_disk, lv_color_hex(0xffa500), 0);
    lv_obj_align(lbl_disk, LV_ALIGN_TOP_LEFT, 12, 8);

    label_disk = lv_label_create(cont_disk);
    lv_label_set_text(label_disk, "-- GB / -- GB");
    lv_obj_set_style_text_font(label_disk, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label_disk, lv_color_hex(0xffffff), 0);
    lv_obj_align(label_disk, LV_ALIGN_LEFT_MID, 12, 8);

    return page;
}

/**
 * Build Page 3: Volume Controls
 * Shows volume bar, +/- buttons, and mute toggle.
 */
static lv_obj_t* build_page_volume(lv_obj_t *parent) {
    lv_obj_t *page = lv_obj_create(parent);
    lv_obj_set_size(page, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(page, 16, 0);
    lv_obj_set_style_border_width(page, 0, 0);
    lv_obj_set_style_bg_color(page, lv_color_hex(0x16213e), 0);

    // Title
    lv_obj_t *title = lv_label_create(page);
    lv_label_set_text(title, "Volume Control");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xffffff), 0);
    lv_obj_set_width(title, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_bottom(title, 20, 0);

    // Volume percentage display
    label_volume = lv_label_create(page);
    lv_label_set_text(label_volume, "--%");
    lv_obj_set_style_text_font(label_volume, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(label_volume, lv_color_hex(0x00d2ff), 0);
    lv_obj_set_width(label_volume, DISPLAY_WIDTH);
    lv_obj_set_style_text_align(label_volume, LV_TEXT_ALIGN_CENTER, 0);

    // Volume bar
    bar_volume = lv_bar_create(page);
    lv_obj_set_size(bar_volume, DISPLAY_WIDTH - 64, 24);
    lv_obj_center(bar_volume);
    lv_bar_set_range(bar_volume, 0, 100);
    lv_bar_set_value(bar_volume, 75, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_volume, lv_color_hex(0x333355), 0);
    lv_obj_set_style_bg_grad_color(bar_volume, lv_color_hex(0x00d2ff), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_volume, 12, 0);

    // Button container
    lv_obj_t *btn_cont = lv_obj_create(page);
    lv_obj_set_size(btn_cont, DISPLAY_WIDTH - 32, 160);
    lv_obj_set_style_border_width(btn_cont, 0, 0);
    lv_obj_set_style_bg_opa(btn_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn_cont, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_cont, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Volume Up button
    btn_vol_up = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_vol_up, 120, 120);
    lv_obj_set_style_radius(btn_vol_up, 60, 0);
    lv_obj_set_style_bg_color(btn_vol_up, lv_color_hex(0x0066cc), 0);
    lv_obj_set_style_shadow_width(btn_vol_up, 8, 0);
    lv_obj_set_style_shadow_color(btn_vol_up, lv_color_hex(0x003366), 0);
    lv_obj_t *lbl_up = lv_label_create(btn_vol_up);
    lv_label_set_text(lbl_up, "+");
    lv_obj_set_style_text_font(lbl_up, &lv_font_montserrat_32, 0);
    lv_obj_center(lbl_up);
    lv_obj_add_event_cb(btn_vol_up, [](lv_event_t *) { volume_up(); },
                        LV_EVENT_CLICKED, nullptr);

    // Volume Down button
    btn_vol_down = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_vol_down, 120, 120);
    lv_obj_set_style_radius(btn_vol_down, 60, 0);
    lv_obj_set_style_bg_color(btn_vol_down, lv_color_hex(0x0066cc), 0);
    lv_obj_set_style_shadow_width(btn_vol_down, 8, 0);
    lv_obj_set_style_shadow_color(btn_vol_down, lv_color_hex(0x003366), 0);
    lv_obj_t *lbl_down = lv_label_create(btn_vol_down);
    lv_label_set_text(lbl_down, "\u2212");
    lv_obj_set_style_text_font(lbl_down, &lv_font_montserrat_32, 0);
    lv_obj_center(lbl_down);
    lv_obj_add_event_cb(btn_vol_down, [](lv_event_t *) { volume_down(); },
                        LV_EVENT_CLICKED, nullptr);

    // Mute toggle button
    btn_vol_mute = lv_btn_create(btn_cont);
    lv_obj_set_size(btn_vol_mute, 120, 120);
    lv_obj_set_style_radius(btn_vol_mute, 60, 0);
    lv_obj_set_style_bg_color(btn_vol_mute, lv_color_hex(0xcc4400), 0);
    lv_obj_set_style_shadow_width(btn_vol_mute, 8, 0);
    lv_obj_set_style_shadow_color(btn_vol_mute, lv_color_hex(0x662200), 0);
    lv_obj_t *lbl_mute = lv_label_create(btn_vol_mute);
    lv_label_set_text(lbl_mute, "MUTE");
    lv_obj_set_style_text_font(lbl_mute, &lv_font_montserrat_16, 0);
    lv_obj_center(lbl_mute);
    lv_obj_add_event_cb(btn_vol_mute, [](lv_event_t *) { volume_toggle_mute(); },
                        LV_EVENT_CLICKED, nullptr);

    return page;
}

/**
 * Create a horizontal swipeable layout with 3 pages and a dot indicator.
 */
static void build_ui() {
    // Create a container for the 3 pages, placed side-by-side horizontally
    page_container = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page_container, DISPLAY_WIDTH * 3, DISPLAY_HEIGHT);
    lv_obj_set_pos(page_container, 0, 0);
    lv_obj_set_style_border_width(page_container, 0, 0);
    lv_obj_set_style_bg_opa(page_container, LV_OPA_TRANSP, 0);
    // Disable scroll on the outer container - we handle page changes manually
    lv_obj_remove_flag(page_container, LV_OBJ_FLAG_SCROLLABLE);

    // Build the 3 pages inside the container
    lv_obj_t *p1 = build_page_workspaces(page_container);
    lv_obj_set_pos(p1, 0, 0);

    lv_obj_t *p2 = build_page_system(page_container);
    lv_obj_set_pos(p2, DISPLAY_WIDTH, 0);

    lv_obj_t *p3 = build_page_volume(page_container);
    lv_obj_set_pos(p3, DISPLAY_WIDTH * 2, 0);

    // --- Page indicator dots (at bottom of screen) ---
    page_indicator = lv_obj_create(lv_scr_act());
    lv_obj_set_size(page_indicator, DISPLAY_WIDTH, 30);
    lv_obj_align(page_indicator, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_border_width(page_indicator, 0, 0);
    lv_obj_set_style_bg_opa(page_indicator, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(page_indicator, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(page_indicator, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    // Create 3 dot indicators
    for (int i = 0; i < 3; i++) {
        lv_obj_t *dot = lv_obj_create(page_indicator);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot, i == 0 ? lv_color_hex(0x00d2ff)
                                              : lv_color_hex(0x555577), 0);
        lv_obj_set_style_margin_left(dot, 6, 0);
        lv_obj_set_style_margin_right(dot, 6, 0);
        lv_obj_set_user_data(dot, (void *)(intptr_t)i);
    }

    // Add swipe gesture detection on the main screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, [](lv_event_t *e) {
        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;

        lv_point_t vect;
        lv_indev_get_vect(indev, &vect);

        // Require a minimum swipe distance to avoid accidental triggers
        if (abs(vect.x) < 30) return;

        int dir = (vect.x < 0) ? 1 : -1;  // left swipe = next, right swipe = prev
        int new_page = current_page + dir;

        if (new_page < 0 || new_page > 2) return;  // out of bounds

        current_page = new_page;
        switch_page(current_page, true);
    }, LV_EVENT_GESTURE, nullptr);

    // Mark initial page as active
    switch_page(0, false);
}

/**
 * Animate to the given page index (0, 1, or 2).
 * @param animated  if true, animate the transition with an easing curve
 */
void switch_page(int page, bool animated) {
    // Clamp
    if (page < 0) page = 0;
    if (page > 2) page = 2;

    int target_x = -(page * DISPLAY_WIDTH);

    if (animated) {
        // Animate with LVGL animator for smooth transition
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, page_container);
        lv_anim_set_exec_cb(&a, [](void *obj, int32_t v) {
            lv_obj_set_pos((lv_obj_t *)obj, v, 0);
        });
        lv_anim_set_values(&a, lv_obj_get_x(page_container), target_x);
        lv_anim_set_time(&a, 300);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
        lv_anim_start(&a);
    } else {
        lv_obj_set_pos(page_container, target_x, 0);
    }

    // Update indicator dots
    lv_obj_t *dots = page_indicator;
    uint32_t child_count = lv_obj_get_child_count(dots);
    for (uint32_t i = 0; i < child_count; i++) {
        lv_obj_t *dot = lv_obj_get_child(dots, i);
        lv_obj_set_style_bg_color(dot,
            (int)i == page ? lv_color_hex(0x00d2ff) : lv_color_hex(0x555577), 0);
    }
}

// ============================================================================
// LVGL Initialization
// ============================================================================

static void init_lvgl() {
    Serial.println("[LVGL] Initializing...");
    lv_init();

    // NOTE: LV_TICK_CUSTOM is defined in lv_conf.h (compile-time tick from millis()).
    // The runtime call lv_tick_set_cb() is redundant when LV_TICK_CUSTOM is active.
    // We keep it as a fallback in case LV_TICK_CUSTOM is ever disabled.
    lv_tick_set_cb(lvgl_tick_cb);

    // Allocate a single draw buffer from DMA RAM (no PSRAM available)
    // LVGL 9.x lv_display_set_buffers() expects the size in PIXELS, not bytes!
    size_t buf_size_px = DISPLAY_WIDTH * LVGL_BUF_ROWS;        // pixels per buffer
    size_t buf_size_bytes = buf_size_px * sizeof(lv_color_t);   // actual bytes needed

    Serial.printf("[LVGL] Buffer: %d x %d rows = %d pixels, %zu bytes (single buffer)\n",
                  DISPLAY_WIDTH, LVGL_BUF_ROWS, buf_size_px, buf_size_bytes);

    // Allocate ONE display buffer (single buffering to save RAM)
    // Try: DMA-capable SRAM (~384KB available) → normal malloc (DRAM, limited ~52KB)
    // Note: 800 x 160 x 2 = 256KB fits easily in DMA RAM
    const char *alloc_source = "none";

    lvgl_draw_buf = (lv_color_t *)heap_caps_malloc(buf_size_bytes * 1, MALLOC_CAP_DMA);
    if (lvgl_draw_buf) {
        alloc_source = "DMA RAM (heap_caps_malloc)";
    } else {
        Serial.println("[LVGL] WARNING: DMA RAM failed, trying normal malloc");
        lvgl_draw_buf = (lv_color_t *)malloc(buf_size_bytes * 1);
        if (lvgl_draw_buf) {
            alloc_source = "normal RAM (malloc)";
        }
    }

    if (!lvgl_draw_buf) {
        Serial.println("[LVGL] FATAL: Cannot allocate draw buffer from ANY source! HALTING.");
        while (1) { delay(100); }
    }
    Serial.printf("[LVGL] Draw buffer allocated via %s at 0x%08x (%zu bytes total)\n",
                  alloc_source, (uintptr_t)lvgl_draw_buf, buf_size_bytes * 1);

    lvgl_disp = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_flush_cb(lvgl_disp, lvgl_flush_cb);

    // Use a single buffer (no PSRAM available — saves 96KB vs double buffer)
    // CRITICAL: third arg is SIZE IN PIXELS, not bytes!
    lv_color_t *buf1 = lvgl_draw_buf;
    lv_color_t *buf2 = NULL;  // single buffer mode
    lv_display_set_buffers(lvgl_disp, buf1, buf2,
                           buf_size_px, LV_DISPLAY_RENDER_MODE_PARTIAL);

    Serial.printf("[LVGL] Display registered with %d px buffer, mode=PARTIAL, depth=%d\n",
                  buf_size_px, LV_COLOR_DEPTH);

    // Register touch input device
    lvgl_indev = lv_indev_create();
    lv_indev_set_type(lvgl_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_indev, lvgl_touch_read_cb);

    Serial.println("[LVGL] Initialized OK");
}

// ============================================================================
// API Data Parsing & UI Updates
// ============================================================================

/**
 * Parse workspace data from JSON and update the workspace grid labels.
 * Expected JSON format:
 * {
 *   "workspaces": [
 *     {"id": 1, "name": "Workspace 1", "windows": ["Firefox", "Terminal"]},
 *     ...
 *   ]
 * }
 */
static void parse_and_update_workspaces(JsonDocument &json) {
    JsonArray workspaces = json["workspaces"].as<JsonArray>();
    if (workspaces.isNull()) return;

    for (int i = 0; i < NUMBER_OF_WORKSPACES && i < (int)workspaces.size(); i++) {
        JsonObject ws = workspaces[i];
        const char *name = ws["name"] | "WS";
        JsonArray windows = ws["windows"].as<JsonArray>();
        int win_count = windows.size();

        // Build window list string (limit to first few windows)
        String win_list;
        for (int w = 0; w < win_count && w < 4; w++) {
            if (w > 0) win_list += "\n";
            win_list += String(windows[w].as<const char *>());
        }
        if (win_count > 4) {
            win_list += "\n...";
        }
        if (win_count == 0) {
            win_list = "(empty)";
        }

        lv_label_set_text_fmt(ws_labels[i], "%s\n%d open\n%s",
                              name, win_count, win_list.c_str());
    }
}

/**
 * Parse system monitor data and update labels.
 * Expected JSON format:
 * {
 *   "cpu_temp": 45.2,
 *   "ram_used": 4.2, "ram_total": 15.6,
 *   "disk_used": 120, "disk_total": 512
 * }
 */
static void parse_and_update_system(JsonDocument &json) {
    float cpu_temp = json["cpu_temp"] | NAN;
    float ram_used = json["ram_used"] | NAN;
    float ram_total = json["ram_total"] | NAN;
    float disk_used = json["disk_used"] | NAN;
    float disk_total = json["disk_total"] | NAN;

    if (!isnan(cpu_temp)) {
        lv_label_set_text_fmt(label_cpu_temp, "%.1f\u00b0C", cpu_temp);
    }
    if (!isnan(ram_used) && !isnan(ram_total)) {
        lv_label_set_text_fmt(label_ram, "%.1f GB / %.1f GB", ram_used, ram_total);
    }
    if (!isnan(disk_used) && !isnan(disk_total)) {
        lv_label_set_text_fmt(label_disk, "%.0f GB / %.0f GB", disk_used, disk_total);
    }
}

/**
 * Parse volume data and update UI.
 * Expected JSON format:
 * {
 *   "volume": 75,
 *   "muted": false
 * }
 */
static void parse_and_update_volume(JsonDocument &json) {
    volume_level = json["volume"] | volume_level;
    volume_muted = json["muted"] | volume_muted;

    if (volume_muted) {
        lv_label_set_text(label_volume, "MUTED");
        lv_obj_set_style_text_color(label_volume, lv_color_hex(0xff4444), 0);
        lv_obj_set_style_bg_color(btn_vol_mute, lv_color_hex(0xcc0000), 0);
    } else {
        lv_label_set_text_fmt(label_volume, "%d%%", volume_level);
        lv_obj_set_style_text_color(label_volume, lv_color_hex(0x00d2ff), 0);
        lv_obj_set_style_bg_color(btn_vol_mute, lv_color_hex(0xcc4400), 0);
    }
    lv_bar_set_value(bar_volume, volume_level, LV_ANIM_OFF);
}

/**
 * Fetch all dashboard data from the API and update the UI.
 */
static void api_poll_all() {
    // Fetch workspaces
    {
        JsonDocument doc;
        if (http_get(API_WORKSPACES, doc)) {
            parse_and_update_workspaces(doc);
        }
    }

    // Fetch system info
    {
        JsonDocument doc;
        if (http_get(API_SYSTEM, doc)) {
            parse_and_update_system(doc);
        }
    }

    // Fetch volume state
    {
        JsonDocument doc;
        if (http_get(API_VOLUME, doc)) {
            parse_and_update_volume(doc);
        }
    }
}

// ============================================================================
// Action Handlers (called from UI events)
// ============================================================================

static void switch_workspace(int id) {
    Serial.printf("[ACTION] Switch to workspace %d\n", id);
    JsonDocument body;
    body["id"] = id;
    http_post(API_SWITCH_WORKSPACE, &body);
}

static void volume_up() {
    Serial.println("[ACTION] Volume up");
    http_post(API_VOLUME_UP);
    // Immediately update UI optimistically, will be corrected on next poll
    volume_level = min(100, volume_level + 5);
    lv_bar_set_value(bar_volume, volume_level, LV_ANIM_ON);
    lv_label_set_text_fmt(label_volume, "%d%%", volume_level);
}

static void volume_down() {
    Serial.println("[ACTION] Volume down");
    http_post(API_VOLUME_DOWN);
    volume_level = max(0, volume_level - 5);
    lv_bar_set_value(bar_volume, volume_level, LV_ANIM_ON);
    lv_label_set_text_fmt(label_volume, "%d%%", volume_level);
}

static void volume_toggle_mute() {
    Serial.println("[ACTION] Toggle mute");
    http_post(API_VOLUME_MUTE);
    volume_muted = !volume_muted;
    if (volume_muted) {
        lv_label_set_text(label_volume, "MUTED");
        lv_obj_set_style_text_color(label_volume, lv_color_hex(0xff4444), 0);
    } else {
        lv_label_set_text_fmt(label_volume, "%d%%", volume_level);
        lv_obj_set_style_text_color(label_volume, lv_color_hex(0x00d2ff), 0);
    }
}

// ============================================================================
// Arduino Setup & Loop
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\\n========================================");
    Serial.println("ESP32 Dashboard Controller");
    Serial.println("Sunton ESP32-8048S070 (800x480)");
    Serial.println("========================================");

    // ====================================================
    // PSRAM INIT — MUST happen before display / LVGL init
    // On ESP32-S3 with octal PSRAM (Sunton 8048S070), the
    // bootloader may not auto-init PSRAM.  psramInit() must
    // be called explicitly, and early, to ensure the display
    // buffer allocator (ps_malloc) has PSRAM available.
    // ====================================================
#if defined(BOARD_HAS_PSRAM)
    {
        Serial.println("[PSRAM] Calling psramInit()...");
        bool psram_ok = psramInit();
        Serial.printf("[PSRAM] psramInit() returned: %s\\n",
                      psram_ok ? "SUCCESS" : "FAILED");
    }
#else
    Serial.println("[PSRAM] BOARD_HAS_PSRAM not defined — skipping psramInit()");
#endif

    if (psramFound()) {
        Serial.printf("[PSRAM] FOUND: %u bytes total, %u bytes free\\n",
                      ESP.getPsramSize(), ESP.getFreePsram());
        Serial.printf("[PSRAM] = %u MB total, %u MB free\\n",
                      ESP.getPsramSize() / 1048576,
                      ESP.getFreePsram() / 1048576);
    } else {
        Serial.println("[PSRAM] NOT FOUND! Display performance will be severely degraded.");
        Serial.println("[PSRAM] Check platformio.ini: board_build.psram=enable, psram_mode=opi");
        Serial.println("[PSRAM] Reason: Sunton ESP32-8048S070 uses octal PSRAM, not quad SPI.");
    }

    Serial.printf("[CHIP] Model: %s Rev %d, Cores: %d, Freq: %d MHz\\n",
                  ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getChipCores(), ESP.getCpuFreqMHz());
    Serial.printf("[CHIP] Flash: %u MB\\n",
                  ESP.getFlashChipSize() / 1048576);

    // 1. Initialize display (and touch via LovyanGFX)
    init_display();

    // 2. Initialize LVGL
    init_lvgl();

    // 3. Build the UI
    Serial.println("[UI] Building UI...");
    build_ui();
    Serial.println("[UI] UI built OK");

    // 4. Connect to WiFi (non-blocking would be better, but keeping it simple)
    // NOTE: If WiFi doesn't exist, this blocks for WIFI_TIMEOUT_MS (10s).
    // The UI will display regardless of WiFi status.
    Serial.println("[WIFI] Connecting (non-blocking eventually)...");
    wifi_connect();

    // 5. Initial data fetch (skips gracefully if WiFi is down)
    if (WiFi.status() == WL_CONNECTED) {
        api_poll_all();
    } else {
        Serial.println("[API] Skipping initial poll (WiFi not connected)");
    }

    Serial.println("[SETUP] Complete! Entering main loop.");
}

void loop() {
    // Run LVGL task handler - THIS IS WHAT MAKES LVGL DRAW!
    // Without this call, LVGL never renders anything to the display.
    static unsigned long last_loop_print = 0;
    unsigned long now = millis();

    lv_timer_handler();

    // Print a heartbeat every 5 seconds to confirm loop() is running
    if (now - last_loop_print > 5000) {
        last_loop_print = now;
        Serial.printf("[LOOP] Alive at %lu ms, free heap=%d, free PSRAM=%d\n",
                      now, ESP.getFreeHeap(), ESP.getFreePsram());
    }

    // Small delay to prevent watchdog timeout on ESP32-S3
    // This is standard practice (5ms is fine for 200Hz LVGL polling)
    delay(5);

    // Ensure WiFi stays connected
    wifi_ensure_connected();

    // Poll the API at the configured interval
    if (now - last_poll_ms >= API_POLL_INTERVAL_MS) {
        last_poll_ms = now;
        api_poll_all();
    }
}