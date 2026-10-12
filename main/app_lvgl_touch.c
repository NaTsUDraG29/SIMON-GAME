#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "app_lvgl_touch.h"
#include "lcd.h"

static const char *TAG = "APP_UI";

// Periodic timer callback to provide LVGL with a steady time base
static void lv_tick_task(void *arg) {
    lv_tick_inc(2); // 2ms increment
}

static lv_obj_t *s_btn_count = NULL;
static lv_obj_t *s_btn_reset = NULL;
static lv_obj_t *s_count_label = NULL;
static lv_obj_t *s_status_label = NULL;

// --- THE EVENT HANDLER ---
// LVGL calls this function automatically whenever the button is interacted with
static void btn_shared_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e); // Get the object that triggered the event

    // Check if the event was a full press-and-release (a "Click")
    if(code == LV_EVENT_CLICKED) {
        static uint32_t cnt = 0;
        cnt++;

        // Get the label (which is the first child of the button)
        lv_obj_t * btn_label  = lv_obj_get_child(btn, 0);
        
        // Update the text dynamically
        
        
        if (btn == s_btn_count) {
             // 1. Update the label inside the button
            lv_label_set_text_fmt(s_count_label, "Count: %ld", (long)cnt);
            
            // 2. Update external status card
            if (s_status_label) {
                lv_label_set_text_fmt(s_status_label, "Incremented! Value is now %ld", (long)cnt);
                lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x34D399), LV_PART_MAIN); // Green
             }
            ESP_LOGI(TAG, "Count: %ld", (long)cnt);
        }
        else if(btn == s_btn_reset )
         {
            cnt = 0;
            
            // 1. Update counter button's text back to default
            lv_label_set_text(s_count_label, "Count: 0");
            
            // 2. Update status card with reset notification
            if (s_status_label) {
                lv_label_set_text(s_status_label, "Counter reset back to 0!");
                lv_obj_set_style_text_color(s_status_label, lv_color_hex(0xF87171), LV_PART_MAIN); // Red
            }
            ESP_LOGI(TAG, "Counter Reset");
        }

        ESP_LOGI(TAG, "Button clicked %ld times!", (long)cnt);
    }
}

// --- THE LVGL HEARTBEAT ---
static void gui_task(void *pvParameter) {
    ESP_LOGI(TAG, "GUI Task running...");
    while (1) {
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        if (delay_ms > 20) delay_ms = 20;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// --- APPLICATION ENTRY POINT ---
esp_err_t app_lcd_touch_init(void) {
    ESP_LOGI(TAG, "Building Interactive UI and starting GUI Task...");

    // 0. Start hardware periodic timer for LVGL tick (every 2 ms)
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2000)); // 2000 us = 2 ms

    lv_obj_t *scr = lv_scr_act();

    // 1. Sleek Modern Dark Theme Background (fills full 480x320 display)
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0F172A), LV_PART_MAIN); // Deep Slate Navy
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // 2. Header Title
    // lv_obj_t *title = lv_label_create(scr);
    // lv_label_set_text(title, "Rotimatic EB1.1 Touch Panel");
    // lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    // lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    // 3. Subtitle / System Information
    lv_obj_t *subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "ST7796S 480x320 LCD  |  Goodix GT911 Touch");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x60A5FA), LV_PART_MAIN); // Bright Blue
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 42);

    // Button 1 (Left side: 100 pixels to the left of center)
    s_btn_count = lv_btn_create(scr);
    lv_obj_set_size(s_btn_count, 180, 70);
    lv_obj_align(s_btn_count, LV_ALIGN_CENTER, -100, 0); // X = -100, Y = 0

    // Button Styling (Rounded corners, modern cobalt blue, drop shadow, crisp border)
    lv_obj_set_style_radius(s_btn_count, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_count, lv_color_hex(0x2563EB), LV_PART_MAIN); // Royal Blue
    lv_obj_set_style_border_color(s_btn_count, lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_count, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_btn_count, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_btn_count, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_btn_count, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s_btn_count, 4, LV_PART_MAIN);

    lv_obj_add_event_cb(s_btn_count, btn_shared_cb, LV_EVENT_ALL, NULL);

    s_count_label = lv_label_create(s_btn_count);
    lv_label_set_text(s_count_label, "TAP TO INC");
    lv_obj_set_style_text_color(s_count_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(s_count_label);

    // Button 2 (Right side: 100 pixels to the right of center)
    s_btn_reset  = lv_btn_create(scr);
    lv_obj_set_size(s_btn_reset , 180, 70);
    lv_obj_align(s_btn_reset , LV_ALIGN_CENTER, 100, 0);  // X = +100, Y = 0

    lv_obj_set_style_radius(s_btn_reset , 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_reset , lv_color_hex(0x2563EB), LV_PART_MAIN); // Royal Blue
    lv_obj_set_style_border_color(s_btn_reset , lv_color_hex(0x3B82F6), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_reset , 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(s_btn_reset , 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(s_btn_reset , lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(s_btn_reset , LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(s_btn_reset , 4, LV_PART_MAIN);

    // Attach click event callback
   
    lv_obj_add_event_cb(s_btn_reset , btn_shared_cb, LV_EVENT_ALL, NULL);

    // Label inside right button
    lv_obj_t *reset_label = lv_label_create(s_btn_reset);
    lv_label_set_text(reset_label, "TAP TO RESET");
    lv_obj_set_style_text_color(reset_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(reset_label);

    // 5. Status / Diagnostic Info Card at the bottom
    lv_obj_t *info_card = lv_obj_create(scr);
    lv_obj_set_size(info_card, 380, 48);
    lv_obj_align(info_card, LV_ALIGN_BOTTOM_MID, 0, -20);
    lv_obj_set_style_bg_color(info_card, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_color(info_card, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_width(info_card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(info_card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(info_card, LV_OBJ_FLAG_SCROLLABLE);

    s_status_label = lv_label_create(info_card);
    lv_label_set_text(s_status_label, "Status: Ready (Touch anywhere to verify)");
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x94A3B8), LV_PART_MAIN); // Light Slate
    lv_obj_center(s_status_label);

    // 6. Spawn the engine task
    if(xTaskCreatePinnedToCore(gui_task, "gui_task", 8192, NULL, 5, NULL, 1) != pdPASS)
    {
        return ESP_FAIL;
    }

    return ESP_OK;
}