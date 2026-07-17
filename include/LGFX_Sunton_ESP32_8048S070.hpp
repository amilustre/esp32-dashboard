/*
 * LGFX_Sunton_ESP32_8048S070.hpp
 *
 * LovyanGFX board configuration for Sunton ESP32-8048S070
 * 7" 800x480 RGB TFT + GT911 capacitive touch
 * ESP32-S3 with 8MB PSRAM
 *
 * Panel driver IC: EK9716 (RGB parallel)
 * Touch: GT911 (I2C)
 *
 * Based on the known-working configuration from the LovyanGFX community.
 * Uses 12 MHz PCLK to avoid WiFi-related display flickering.
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
public:
  lgfx::Bus_RGB     _bus_instance;
  lgfx::Panel_RGB   _panel_instance;
  lgfx::Light_PWM   _light_instance;
  lgfx::Touch_GT911 _touch_instance;

  LGFX(void)
  {
    // -----------------------------------------------------------------------
    // RGB Parallel Bus Configuration
    // 16-bit RGB565: 5 red + 6 green + 5 blue
    // Pin order (d0-d15): B0..B4, G0..G5, R0..R4
    // -----------------------------------------------------------------------
    {
      auto cfg = _bus_instance.config();

      // Blue  (5 bits) -> d0..d4
      cfg.pin_d0  = 15;   // B0
      cfg.pin_d1  = 7;    // B1
      cfg.pin_d2  = 6;    // B2
      cfg.pin_d3  = 5;    // B3
      cfg.pin_d4  = 4;    // B4

      // Green (6 bits) -> d5..d10
      cfg.pin_d5  = 9;    // G0
      cfg.pin_d6  = 46;   // G1
      cfg.pin_d7  = 3;    // G2
      cfg.pin_d8  = 8;    // G3
      cfg.pin_d9  = 16;   // G4
      cfg.pin_d10 = 1;    // G5

      // Red   (5 bits) -> d11..d15
      cfg.pin_d11 = 14;   // R0
      cfg.pin_d12 = 21;   // R1
      cfg.pin_d13 = 47;   // R2
      cfg.pin_d14 = 48;   // R3
      cfg.pin_d15 = 45;   // R4

      // Control signals
      cfg.pin_henable = 41;   // DE
      cfg.pin_vsync   = 40;   // VSYNC
      cfg.pin_hsync   = 39;   // HSYNC
      cfg.pin_pclk    = 42;   // PCLK

      // Clock frequency: 12 MHz for stable operation with WiFi
      cfg.freq_write = 12000000;

      // Display timing (from Sunton ESP32-8048S070 specs)
      cfg.hsync_polarity    = 0;
      cfg.hsync_front_porch = 20;
      cfg.hsync_pulse_width = 30;
      cfg.hsync_back_porch  = 16;
      cfg.vsync_polarity    = 0;
      cfg.vsync_front_porch = 22;
      cfg.vsync_pulse_width = 13;
      cfg.vsync_back_porch  = 10;
      cfg.pclk_active_neg   = 1;
      cfg.de_idle_high      = 0;
      cfg.pclk_idle_high    = 0;

      _bus_instance.config(cfg);
    }

    // -----------------------------------------------------------------------
    // Panel Configuration
    // -----------------------------------------------------------------------
    {
      auto cfg = _panel_instance.config();

      cfg.memory_width  = 800;
      cfg.memory_height = 480;
      cfg.panel_width   = 800;
      cfg.panel_height  = 480;
      cfg.offset_x      = 0;
      cfg.offset_y      = 0;

      _panel_instance.config(cfg);
    }

    // -----------------------------------------------------------------------
    // Backlight (PWM on GPIO 2)
    // -----------------------------------------------------------------------
    {
      auto cfg = _light_instance.config();

      cfg.pin_bl      = 2;
      cfg.invert      = false;
      cfg.freq        = 44100;
      cfg.pwm_channel = 0;

      _light_instance.config(cfg);
    }

    // -----------------------------------------------------------------------
    // GT911 Touch (I2C)
    // -----------------------------------------------------------------------
    {
      auto cfg = _touch_instance.config();

      cfg.x_min      = 0;
      cfg.x_max      = 799;
      cfg.y_min      = 0;
      cfg.y_max      = 479;
      cfg.pin_int    = -1;   // INT not connected on this board
      cfg.pin_rst    = 38;
      cfg.pin_sda    = 19;
      cfg.pin_scl    = 20;
      cfg.i2c_addr   = 0x5D; // GT911 alternate address (0x14 shifted)
      cfg.i2c_port   = 0;    // I2C_NUM_0
      cfg.freq       = 400000;
      cfg.offset_rotation = 0;

      _touch_instance.config(cfg);
    }

    // Wire everything together
    setPanel(&_panel_instance);
    setBus(&_bus_instance);
    setLight(&_light_instance);
    setTouch(&_touch_instance);
  }
};
