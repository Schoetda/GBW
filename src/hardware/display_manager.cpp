#include "display_manager.h"
#include "../config/constants.h"
#include "../config/logging.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include <cstring>

DisplayManager* g_display_manager = nullptr;

void DisplayManager::init() {
    g_display_manager = this;
    
    // Get active display config
    const DisplayConfig& config = ACTIVE_DISPLAY;

    // Initialize display hardware
    init_display_hardware(config);
    
    if (!gfx_device || !gfx_device->begin()) {
        return;
    }
    
    gfx_device->fillScreen(RGB565_BLACK);
    
    // Initialize LVGL
    lv_init();
    lv_tick_set_cb(millis_cb);
    
    screen_width = gfx_device->width();
    screen_height = gfx_device->height();

    // Full screen buffer, but only partial updates used
    // RGB565 format (16bit per pixel)
    draw_buffer = nullptr;
    dma_staging_buffer = nullptr;
    dma_staging_rows = 16;

    const size_t draw_rows = 40; // 280 * 40 * 2 = 22,400 bytes
    buffer_size = screen_width * draw_rows * sizeof(uint16_t);

    draw_buffer = static_cast<lv_color_t*>(
        heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                buffer_size,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (!draw_buffer) {
        draw_buffer = static_cast<lv_color_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    buffer_size,
                                    MALLOC_CAP_8BIT));
    }

    if (!draw_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate LVGL draw buffer\n");
        return;
    }

    while (dma_staging_rows >= 4 && dma_staging_buffer == nullptr) {
        dma_staging_buffer = static_cast<uint16_t*>(
            heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN,
                                    screen_width * dma_staging_rows * sizeof(uint16_t),
                                    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT));
        if (!dma_staging_buffer) {
            dma_staging_rows /= 2;
        }
    }

    if (!dma_staging_buffer) {
        LOG_BLE("[DISPLAY] ERROR: Failed to allocate DMA staging buffer\n");
        heap_caps_free(draw_buffer);
        draw_buffer = nullptr;
        return;
    }

    lvgl_display = lv_display_create(screen_width, screen_height);
    lv_display_set_flush_cb(lvgl_display, display_flush_cb);
    lv_display_set_buffers(lvgl_display, draw_buffer, NULL,
                          buffer_size , LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_display_add_event_cb(lvgl_display, display_rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    
    if (config.is_round) {
        lv_obj_set_style_clip_corner(lv_scr_act(), true, 0);
    }

    // Initialize touch
    touch_driver.init();
    lvgl_input = lv_indev_create();
    lv_indev_set_type(lvgl_input, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(lvgl_input, touchpad_read_cb);
    
    initialized = true;
}

void DisplayManager::init_display_hardware(const DisplayConfig& config) {
    switch (config.interface) {
        case DisplayInterfaceType::QSPI:
            init_qspi_display(config);
            break;
        case DisplayInterfaceType::MIPI_PARALLEL:
            init_mipi_parallel_display(config);
            break;
        // Add more as needed
    }
}

void DisplayManager::init_qspi_display(const DisplayConfig& config) {
    bus = new Arduino_ESP32QSPI(
        config.pins.cs, config.pins.sck, 
        config.pins.d0, config.pins.d1, 
        config.pins.d2, config.pins.d3
    );
    
    if (strcmp(config.driver_name, "CO5300") == 0) {
        gfx_device = new Arduino_CO5300(
            bus, config.pins.rst, config.rotation, 
            config.width, config.height,
            config.color_order, config.offset_x,
            config.ips_invert_x, config.ips_invert_y
        );
    }
    // Add more QSPI drivers as needed
}

void DisplayManager::init_mipi_parallel_display(const DisplayConfig& config) {
    // Handle MIPI_PARALLEL interface (e.g., ST7701)
    
    // see: src/board/supported/viewe/BOARD_VIEWE_UEDX48480021_MD80ET.h
    // see: https://github.com/Witaliy76/Yoradio_RGB_Panel/blob/996d063d1130aa3730d155a91bfb01332932d5c9/src/src/displays/displayUEDX48480021.cpp#L51
    // 0. Initialize RST pin according to UEDX48480021 guide
    if (config.pins.rst >= 0) {
        pinMode(config.pins.rst, OUTPUT);
        digitalWrite(config.pins.rst, HIGH);  // RST HIGH (inactive)
        delay(10);
        digitalWrite(config.pins.rst, LOW);   // RST LOW (reset)
        delay(10);
        digitalWrite(config.pins.rst, HIGH);  // RST HIGH (active)
        delay(50);  // Post-reset delay
    }
    
    bus = new Arduino_SWSPI( //Arduino_ESP32SPI(
        GFX_NOT_DEFINED, // dc
        config.pins.cs, config.pins.sck, config.pins.sda, GFX_NOT_DEFINED
    );

    // Modern Arduino_GFX usage: create the panel, then the display
    Arduino_ESP32RGBPanel* rgbpanel = new Arduino_ESP32RGBPanel(
        config.pins.de, config.pins.vsync, config.pins.hsync, config.pins.pclk,
        config.pins.r0, config.pins.r1, config.pins.r2, config.pins.r3, config.pins.r4,
        config.pins.g0, config.pins.g1, config.pins.g2, config.pins.g3, config.pins.g4, config.pins.g5,
        config.pins.b0, config.pins.b1, config.pins.b2, config.pins.b3, config.pins.b4,
        // Horizontal/vertical timing parameters - CRITICAL for UEDX48480021
        1 /* hsync_polarity */, 50 /* hsync_front_porch */, 8 /* hsync_pulse_width */, 10 /* hsync_back_porch */,
        1 /* vsync_polarity */, 8 /* vsync_front_porch */, 2 /* vsync_pulse_width */, 18, /* vsync_back_porch */

        0, // pclk_active_neg
        16000000UL, // prefer_speed
        // // To increase the upper limit of the PCLK, see: https://docs.espressif.com/projects/esp-faq/en/latest/software-framework/peripherals/lcd.html#how-can-i-increase-the-upper-limit-of-pclk-settings-on-esp32-s3-while-ensuring-normal-rgb-screen-display
        false, // useBigEndian
        0, // de_idle_high
        0, // pclk_idle_high
        0 //480*10  // bounce_buffer_size_px
    );

    // Determine init operations based on round vs rectangular
    const uint8_t* init_ops;
    size_t init_ops_len;
    if (config.is_round) {
        init_ops = st7701_type4_init_operations;
        init_ops_len = sizeof(st7701_type4_init_operations);
    } else {
        init_ops = st7701_type1_init_operations;
        init_ops_len = sizeof(st7701_type1_init_operations);
    }

    gfx_device = new Arduino_RGB_Display(
        config.width, config.height, rgbpanel, config.rotation, 
        true, // auto_flush
        bus,
        config.pins.rst >= 0 ? config.pins.rst : GFX_NOT_DEFINED,
        init_ops, init_ops_len
    );
}

void DisplayManager::update() {
    if (!initialized) return;
    
    touch_driver.update();
    lv_timer_handler();
}

// Update the refresh area to be full width
// This avoids weird artifacts when partial row updates are used
void DisplayManager::display_rounder_cb(lv_event_t* e) {
    lv_area_t* area = (lv_area_t*)lv_event_get_param(e);
    
    area->x1 = 0;
    area->x2 = g_display_manager->screen_width - 1;
}

void DisplayManager::display_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    if (!g_display_manager || !g_display_manager->gfx_device) return;
    
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    uint32_t remaining_rows = h;
    uint32_t current_y = area->y1;
    uint32_t src_row_offset = 0;
    uint16_t* staging = g_display_manager->dma_staging_buffer;
    uint32_t staging_rows = g_display_manager->dma_staging_rows ? g_display_manager->dma_staging_rows : h;
    const uint16_t* src_pixels = reinterpret_cast<const uint16_t*>(px_map);

    while (remaining_rows > 0) {
        uint32_t rows = std::min<uint32_t>(remaining_rows, staging_rows);
        size_t copy_pixels = static_cast<size_t>(w) * rows;
        const uint16_t* chunk_src = src_pixels + (static_cast<size_t>(src_row_offset) * w);

        if (staging) {
            memcpy(staging, chunk_src, copy_pixels * sizeof(uint16_t));

            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, staging, w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, staging, w, rows);
            }
        } else {
            if (LV_COLOR_16_SWAP) {
                g_display_manager->gfx_device->draw16bitBeRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            } else {
                g_display_manager->gfx_device->draw16bitRGBBitmap(area->x1, current_y, const_cast<uint16_t*>(chunk_src), w, rows);
            }
        }

        remaining_rows -= rows;
        src_row_offset += rows;
        current_y += rows;
    }
    
    lv_display_flush_ready(disp);
}

void DisplayManager::touchpad_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    if (!g_display_manager) return;
    
    TouchData touch = g_display_manager->touch_driver.get_touch_data();
    
    if (touch.pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = touch.x;
        data->point.y = touch.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

uint32_t DisplayManager::millis_cb() {
    return millis();
}

void DisplayManager::set_brightness(float brightness) {
    if (!initialized || !gfx_device) return;
    
    const DisplayConfig& config = ACTIVE_DISPLAY;
    
    // Clamp brightness to valid hardware range [0.0, 1.0]
    if (brightness < 0.0f) brightness = 0.0f;
    if (brightness > 1.0f) brightness = 1.0f;
    
    // Apply minimum brightness constraint from config
    float min_brightness = config.min_brightness_pct / 100.0f;
    if (brightness < min_brightness) brightness = min_brightness;
    
    // Set brightness based on driver type
    if (strcmp(config.driver_name, "CO5300") == 0) {
        // CO5300: Use driver's built-in brightness control
        Arduino_CO5300* display = static_cast<Arduino_CO5300*>(gfx_device);
        uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
        display->setBrightness(brightness_value);
    }
    else if (strcmp(config.driver_name, "ST7701") == 0) {
        // ST7701: Use PWM-based backlight control
        if (config.pins.bl >= 0) {
            uint8_t brightness_value = (uint8_t)(brightness * 255.0f);
            // Configure PWM for backlight pin if not already configured
            static bool bl_pwm_configured = false;
            if (!bl_pwm_configured) {
                ledcAttach(config.pins.bl, 5000, 8); // pin, 5kHz, 8-bit
                bl_pwm_configured = true;
            }
            ledcWrite(0, brightness_value);
        }
    }
    // Add other driver types here as needed
}
