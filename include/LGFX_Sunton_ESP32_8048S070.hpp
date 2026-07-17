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
 * Based on the official LovyanGFX board header from:
 * https://github.com/lovyan03/LovyanGFX/blob/master/src/lgfx_user/LGFX_Sunton_ESP32-8048S070.h
 *
 * Uses 12 MHz PCLK to avoid WiFi-related display flickering (ref: LovyanGFX issue #760).
 */
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <driver/i2c.h>

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

    // Use PSRAM for frame buffer
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;
      _panel_instance.config_detail(cfg);
    }

    // -----------------------------------------------------------------------
    // RGB Parallel Bus Configuration
    // 16-bit RGB565: 5 red + 6 green + 5 blue
    // Pin order (d0-d15): B0..B4, G0..G5, R0..R4
    // -----------------------------------------------------------------------
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      // Blue  (5 bits) -> d0..d4
      cfg.pin_d0  = GPIO_NUM_15;   // B0
      cfg.pin_d1  = GPIO_NUM_7;    // B1
      cfg.pin_d2  = GPIO_NUM_6;    // B2
      cfg.pin_d3  = GPIO_NUM_5;    // B3
      cfg.pin_d4  = GPIO_NUM_4;    // B4

      // Green (6 bits) -> d5..d10
      cfg.pin_d5  = GPIO_NUM_9;    // G0
      cfg.pin_d6  = GPIO_NUM_46;   // G1
      cfg.pin_d7  = GPIO_NUM_3;    // G2
      cfg.pin_d8  = GPIO_NUM_8;    // G3
      cfg.pin_d9  = GPIO_NUM_16;   // G4
      cfg.pin_d10 = GPIO_NUM_1;    // G5

      // Red   (5 bits) -> d11..d15
      cfg.pin_d11 = GPIO_NUM_14;   // R0
      cfg.pin_d12 = GPIO_NUM_21;   // R1
      cfg.pin_d13 = GPIO_NUM_47;   // R2
      cfg.pin_d14 = GPIO_NUM_48;   // R3
      cfg.pin_d15 = GPIO_NUM_45;   // R4

      // Control signals
      cfg.pin_henable = GPIO_NUM_41;   // DE
      cfg.pin_vsync   = GPIO_NUM_40;   // VSYNC
      cfg.pin_hsync   = GPIO_NUM_39;   // HSYNC
      cfg.pin_pclk    = GPIO_NUM_42;   // PCLK

      // Clock frequency: 12 MHz for stable operation with WiFi
      // (14 MHz may cause tearing with newer espressif frameworks)
      cfg.freq_write = 12000000;

      // Display timing (from official LovyanGFX board header)
      cfg.hsync_polarity       = 0;
      cfg.hsync_front_porch    = 80;
      cfg.hsync_pulse_width    = 4;
      cfg.hsync_back_porch     = 16;
      cfg.vsync_polarity       = 0;
      cfg.vsync_front_porch    = 22;
      cfg.vsync_pulse_width    = 4;
      cfg.vsync_back_porch     = 4;
      cfg.pclk_idle_high       = 1;

      _bus_instance.config(cfg);
    }

    // Link bus to panel
    _panel_instance.setBus(&_bus_instance);

    // -----------------------------------------------------------------------
    // Backlight (PWM on GPIO 2)
    // -----------------------------------------------------------------------
    {
      auto cfg = _light_instance.config();

      cfg.pin_bl = GPIO_NUM_2;
      cfg.invert = false;
      cfg.freq   = 44100;
      cfg.pwm_channel = 0;

      _light_instance.config(cfg);
    }

    // Link backlight to panel
    _panel_instance.light(&_light_instance);

    // -----------------------------------------------------------------------
    // GT911 Touch (I2C)
    // -----------------------------------------------------------------------
    {
      auto cfg = _touch_instance.config();

      cfg.x_min      = 0;
      cfg.x_max      = 800;
      cfg.y_min      = 0;
      cfg.y_max      = 480;
      cfg.pin_int    = GPIO_NUM_NC;    // INT not connected on this board
      cfg.pin_rst    = GPIO_NUM_38;
      cfg.pin_sda    = GPIO_NUM_19;
      cfg.pin_scl    = GPIO_NUM_20;
      cfg.i2c_addr   = 0x5D;           // GT911 address (0x14 shifted = 0x5D)
      cfg.i2c_port   = I2C_NUM_0;
      cfg.freq       = 400000;
      cfg.offset_rotation = 0;
      cfg.bus_shared = false;

      _touch_instance.config(cfg);
    }

    // Link touch to panel
    _panel_instance.setTouch(&_touch_instance);

    // Register panel with device
    setPanel(&_panel_instance);
  }
};
