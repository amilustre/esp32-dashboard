# ESP32 Dashboard Controller

Firmware for the **Sunton ESP32-8048S070** — a 7" 800×480 capacitive touch display with ESP32-S3 (8MB PSRAM). This turns the display into a dashboard controller for a **NixOS desktop**, showing workspace overview, system monitor stats, and volume controls.

## Hardware

| Component | Details |
|-----------|---------|
| Board     | Sunton ESP32-8048S070 |
| MCU       | ESP32-S3, 240MHz, dual-core |
| Flash     | 16MB |
| PSRAM     | 8MB (OPI PSRAM) — **required** for LVGL display buffer |
| Display   | 7" TFT, 800×480, RGB-565 parallel interface |
| Touch     | GT911 capacitive touch (I2C) |
| Backlight | PWM (GPIO 2) |

### Pin Mapping

| RGB Signal | GPIO | RGB Signal | GPIO |
|-----------|------|-----------|------|
| **DE**    | 41   | **G2**    | 3    |
| **VSYNC** | 40   | **G3**    | 8    |
| **HSYNC** | 39   | **G4**    | 16   |
| **PCLK**  | 42   | **G5**    | 1    |
| **R0**    | 14   | **B0**    | 15   |
| **R1**    | 21   | **B1**    | 7    |
| **R2**    | 47   | **B2**    | 6    |
| **R3**    | 48   | **B3**    | 5    |
| **R4**    | 45   | **B4**    | 4    |
| **G0**    | 9    | **Touch SDA** | 19 |
| **G1**    | 46   | **Touch SCL** | 20 |
| | | **Touch RST** | 38 |

## Features

- **WiFi** — Automatic connect & reconnect
- **API Polling** — Fetches dashboard data every 2 seconds via HTTP
- **3 Swipeable Pages:**

  | Page | Content |
  |------|---------|
  | 1 | **Workspace Overview** — 4×2 grid of 8 workspaces, shows active windows per workspace, tap to switch |
  | 2 | **System Monitor** — CPU temperature, RAM usage, disk usage |
  | 3 | **Volume Controls** — + / − / Mute buttons with volume bar |

- **Smooth page transitions** — Animated with easing curves
- **Touch debouncing** — Clean touch response
- **PSRAM-backed display buffer** — Required for 800×480 resolution

## Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or CLI: `pip install platformio`)
- USB-C cable for flashing

## Build & Flash

```bash
# Clone the repo
git clone https://github.com/amilustre/esp32-dashboard.git
cd esp32-dashboard

# Configure WiFi credentials
# Edit include/config.h and set WIFI_SSID and WIFI_PASSWORD

# Build
pio run

# Flash (connect board via USB-C, may need to hold BOOT button)
pio run --target upload

# Monitor serial output
pio device monitor
```

## Configuration

Edit `include/config.h` to set:

| Setting | Default | Description |
|---------|---------|-------------|
| `WIFI_SSID` | `"YourWiFiSSID"` | WiFi network name |
| `WIFI_PASSWORD` | `"YourWiFiPassword"` | WiFi password |
| `API_HOST` | `192.168.1.33` | Dashboard API server IP |
| `API_PORT` | `8080` | Dashboard API port |
| `API_POLL_INTERVAL_MS` | `2000` | Data refresh interval |

## Dashboard API

The firmware expects the following API endpoints on the NixOS host:

### GET Endpoints (polled every 2s)

| Endpoint | Returns |
|----------|---------|
| `/api/workspaces` | `{"workspaces":[{"id":1,"name":"WS 1","windows":["Firefox"]},...]}` |
| `/api/system` | `{"cpu_temp":45.2,"ram_used":4.2,"ram_total":15.6,"disk_used":120,"disk_total":512}` |
| `/api/volume` | `{"volume":75,"muted":false}` |

### POST Endpoints (triggered by touch)

| Endpoint | Body | Action |
|----------|------|--------|
| `/api/workspace/switch` | `{"id": 3}` | Switch to workspace 3 |
| `/api/volume/up` | `{}` | Increase volume |
| `/api/volume/down` | `{}` | Decrease volume |
| `/api/volume/mute` | `{}` | Toggle mute |

## Tech Stack

- **Framework:** Arduino (via PlatformIO)
- **Board:** `esp32s3box` (ESP32-S3 with PSRAM, compatible with Sunton)
- **UI:** LVGL 9.2+
- **Display Driver:** Arduino_GFX (GFX Library for Arduino 1.5+)
- **Touch:** TAMC_GT911
- **JSON:** ArduinoJson 7.3+

## Project Structure

```
esp32-dashboard/
├── platformio.ini      # PlatformIO build config
├── include/
│   └── config.h        # WiFi, API, pin configuration
├── src/
│   └── main.cpp        # Firmware (LVGL UI, WiFi, HTTP, touch)
└── README.md           # This file
```

## Troubleshooting

- **Display stays black:** Check backlight pin (GPIO 2). Hold BOOT while flashing.
- **WiFi won't connect:** Verify SSID/password in `config.h`. Check that 2.4GHz is enabled.
- **No touch response:** Ensure GT911 is properly connected. The touch uses I2C on GPIO 19/20.
- **Flickering display:** The RGB bus timing in `config.h` should work; if flickering occurs, try adjusting PCLK polarity or frequency in the RGB panel constructor.
- **Brownouts during WiFi:** Add a 470µF capacitor across 5V/GND near the board. ESP32-S3 can draw significant current with WiFi + display active.

## License

MIT
