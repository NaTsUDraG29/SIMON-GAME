#include "simon_ui.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "lvgl.h"
#include "app_events.h"
#include "buzzer.h"

static const char *TAG = "SIMON_UI";

// Global IPC Queues
QueueHandle_t g_user_input_queue = NULL;
QueueHandle_t g_gui_cmd_queue = NULL;

// Screen Objects
static lv_obj_t *s_scr_menu     = NULL;
static lv_obj_t *s_scr_game     = NULL;
static lv_obj_t *s_scr_gameover = NULL;

// Game Screen UI Elements
static lv_obj_t *s_pad_btns[PAD_COUNT];
static lv_obj_t *s_prompt_label         = NULL;
static lv_obj_t *s_score_label          = NULL;
static lv_obj_t *s_round_label          = NULL;
static bool      s_pads_enabled         = false;

// Menu Screen UI Elements
static lv_obj_t *s_menu_high_score_label = NULL;

// Game Over Screen UI Elements
static lv_obj_t *s_final_score_label    = NULL;
static lv_obj_t *s_gameover_high_score_label = NULL;

// Pad Color Palettes: Dim (idle) vs Bright (lit)
static const uint32_t s_pad_colors_dim[PAD_COUNT] = {
    0x065F46, // Green (Dim Emerald)
    0x991B1B, // Red (Dim Ruby)
    0x92400E, // Yellow (Dim Amber)
    0x1E40AF  // Blue (Dim Cobalt)
};

static const uint32_t s_pad_colors_bright[PAD_COUNT] = {
    0x34D399, // Green (Bright Mint)
    0xF87171, // Red (Bright Coral)
    0xFDE047, // Yellow (Bright Gold)
    0x60A5FA  // Blue (Bright Sky)
};

static const char *s_pad_names[PAD_COUNT] = {
    "GREEN", "RED", "YELLOW", "BLUE"
};

// Periodic timer callback to provide LVGL with a steady 2ms tick
static void lv_tick_task(void *arg) {
    lv_tick_inc(2);
}

// =============================================================================
// Helper: Update Pad Visual State (Dim vs Bright)
// =============================================================================
static void simon_ui_set_pad_active(simon_pad_t pad, bool active) {
    if (pad >= PAD_COUNT || s_pad_btns[pad] == NULL) return;

    uint32_t color = active ? s_pad_colors_bright[pad] : s_pad_colors_dim[pad];
    lv_obj_set_style_bg_color(s_pad_btns[pad], lv_color_hex(color), LV_PART_MAIN);

    if (active) {
        lv_obj_set_style_border_color(s_pad_btns[pad], lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_pad_btns[pad], 4, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_pad_btns[pad], 25, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(s_pad_btns[pad], lv_color_hex(color), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(s_pad_btns[pad], LV_OPA_80, LV_PART_MAIN);
    } else {
        lv_obj_set_style_border_color(s_pad_btns[pad], lv_color_hex(0x334155), LV_PART_MAIN);
        lv_obj_set_style_border_width(s_pad_btns[pad], 2, LV_PART_MAIN);
        lv_obj_set_style_shadow_width(s_pad_btns[pad], 10, LV_PART_MAIN);
        lv_obj_set_style_shadow_color(s_pad_btns[pad], lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_shadow_opa(s_pad_btns[pad], LV_OPA_40, LV_PART_MAIN);
    }
}

// =============================================================================
// Button Event Callbacks (Send IPC events to game_logic_task)
// =============================================================================
static void menu_start_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "UI: Start Game clicked -> playing launch tune & sending event");
        buzzer_play(SOUND_GAME_START);
        input_event_t ev = { .type = INPUT_START_GAME };
        xQueueSend(g_user_input_queue, &ev, 0);
    }
}

static void game_pad_click_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        if (!s_pads_enabled) {
            ESP_LOGW(TAG, "UI: Pad touched while Simon is playing! Ignored.");
            return;
        }

        lv_obj_t *btn = lv_event_get_target(e);
        simon_pad_t clicked_pad = PAD_COUNT;

        for (int i = 0; i < PAD_COUNT; i++) {
            if (s_pad_btns[i] == btn) {
                clicked_pad = (simon_pad_t)i;
                break;
            }
        }

        if (clicked_pad < PAD_COUNT) {
            ESP_LOGI(TAG, "UI: Player tapped %s pad (%d) -> playing tone", s_pad_names[clicked_pad], clicked_pad);
            
            // Play matching color pad tone immediately
            buzzer_play((sound_id_t)(SOUND_PAD_GREEN + clicked_pad));

            // Brief local flash feedback for tactile response
            simon_ui_set_pad_active(clicked_pad, true);

            input_event_t ev = {
                .type = INPUT_PAD_CLICKED,
                .pad  = clicked_pad
            };
            xQueueSend(g_user_input_queue, &ev, 0);
        }
    }
}

static void goto_menu_btn_cb(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "UI: Goto Menu clicked");
        input_event_t ev = { .type = INPUT_GOTO_MENU };
        xQueueSend(g_user_input_queue, &ev, 0);
    }
}

// =============================================================================
// SCREEN 1: Main Menu Screen
// =============================================================================
static void create_menu_screen(void) {
    s_scr_menu = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_menu, lv_color_hex(0x0A0F1D), LV_PART_MAIN); // Rich Dark Navy
    lv_obj_set_style_bg_opa(s_scr_menu, LV_OPA_COVER, LV_PART_MAIN);

    // Title: SIMON SAYS
    lv_obj_t *title = lv_label_create(s_scr_menu);
    lv_label_set_text(title, "SIMON SAYS");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_transform_zoom(title, 320, LV_PART_MAIN); // Scale up title
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Subtitle
    lv_obj_t *subtitle = lv_label_create(s_scr_menu);
    lv_label_set_text(subtitle, "Touch & Memory RTOS Challenge");
    lv_obj_set_style_text_color(subtitle, lv_color_hex(0x60A5FA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 85);

    // High Score Badge Card
    lv_obj_t *card = lv_obj_create(s_scr_menu);
    lv_obj_set_size(card, 220, 42);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -5);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_menu_high_score_label = lv_label_create(card);
    lv_label_set_text(s_menu_high_score_label, "BEST SCORE: 0");
    lv_obj_set_style_text_color(s_menu_high_score_label, lv_color_hex(0xFBBF24), LV_PART_MAIN); // Gold
    lv_obj_center(s_menu_high_score_label);

    // START GAME Button
    lv_obj_t *start_btn = lv_btn_create(s_scr_menu);
    lv_obj_set_size(start_btn, 220, 68);
    lv_obj_align(start_btn, LV_ALIGN_CENTER, 0, 75);
    lv_obj_set_style_radius(start_btn, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(start_btn, lv_color_hex(0x10B981), LV_PART_MAIN); // Vibrant Emerald
    lv_obj_set_style_border_color(start_btn, lv_color_hex(0x34D399), LV_PART_MAIN);
    lv_obj_set_style_border_width(start_btn, 2, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(start_btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_color(start_btn, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(start_btn, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_shadow_ofs_y(start_btn, 4, LV_PART_MAIN);
    lv_obj_add_event_cb(start_btn, menu_start_btn_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *start_label = lv_label_create(start_btn);
    lv_label_set_text(start_label, "START GAME");
    lv_obj_set_style_text_color(start_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(start_label);
}

// =============================================================================
// SCREEN 2: Simon Gameplay Screen (480 x 320 Layout)
// =============================================================================
static void create_game_screen(void) {
    s_scr_game = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_game, lv_color_hex(0x0A0F1D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_game, LV_OPA_COVER, LV_PART_MAIN);

    // Top Header: Back to Menu button
    lv_obj_t *menu_btn = lv_btn_create(s_scr_game);
    lv_obj_set_size(menu_btn, 80, 36);
    lv_obj_align(menu_btn, LV_ALIGN_TOP_LEFT, 15, 10);
    lv_obj_set_style_radius(menu_btn, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(menu_btn, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_add_event_cb(menu_btn, goto_menu_btn_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *menu_lbl = lv_label_create(menu_btn);
    lv_label_set_text(menu_lbl, "< EXIT");
    lv_obj_set_style_text_color(menu_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(menu_lbl);

    // Prompt Message Banner (Center of top bar)
    s_prompt_label = lv_label_create(s_scr_game);
    lv_label_set_text(s_prompt_label, "GET READY...");
    lv_obj_set_style_text_color(s_prompt_label, lv_color_hex(0x38BDF8), LV_PART_MAIN);
    lv_obj_align(s_prompt_label, LV_ALIGN_TOP_MID, 0, 18);

    // Score & Round display (Top right)
    s_score_label = lv_label_create(s_scr_game);
    lv_label_set_text(s_score_label, "SCORE: 0");
    lv_obj_set_style_text_color(s_score_label, lv_color_hex(0xFBBF24), LV_PART_MAIN);
    lv_obj_align(s_score_label, LV_ALIGN_TOP_RIGHT, -15, 12);

    s_round_label = lv_label_create(s_scr_game);
    lv_label_set_text(s_round_label, "ROUND 1");
    lv_obj_set_style_text_color(s_round_label, lv_color_hex(0x94A3B8), LV_PART_MAIN);
    lv_obj_align(s_round_label, LV_ALIGN_TOP_RIGHT, -15, 30);

    // 4 Simon Pads in a 2x2 grid centered in the play area (Y: 55..315)
    // Pad dimensions: 175 px width x 115 px height
    const int16_t pad_w = 175;
    const int16_t pad_h = 115;
    const int16_t ofs_x = 100;
    const int16_t ofs_y = 65;
    const int16_t center_y_shift = 15; // Shift down slightly below top bar

    const int16_t pad_coords[PAD_COUNT][2] = {
        { -ofs_x, -ofs_y + center_y_shift }, // Green: Top-Left
        {  ofs_x, -ofs_y + center_y_shift }, // Red: Top-Right
        { -ofs_x,  ofs_y + center_y_shift }, // Yellow: Bottom-Left
        {  ofs_x,  ofs_y + center_y_shift }  // Blue: Bottom-Right
    };

    for (int i = 0; i < PAD_COUNT; i++) {
        s_pad_btns[i] = lv_btn_create(s_scr_game);
        lv_obj_set_size(s_pad_btns[i], pad_w, pad_h);
        lv_obj_align(s_pad_btns[i], LV_ALIGN_CENTER, pad_coords[i][0], pad_coords[i][1]);
        lv_obj_set_style_radius(s_pad_btns[i], 18, LV_PART_MAIN);
        lv_obj_add_event_cb(s_pad_btns[i], game_pad_click_cb, LV_EVENT_ALL, NULL);

        // Apply idle dim color & border
        simon_ui_set_pad_active((simon_pad_t)i, false);

        // Label on pad
        lv_obj_t *lbl = lv_label_create(s_pad_btns[i]);
        lv_label_set_text(lbl, s_pad_names[i]);
        lv_obj_set_style_text_color(lbl, lv_color_white(), LV_PART_MAIN);
        lv_obj_center(lbl);
    }
}

// =============================================================================
// SCREEN 3: Game Over Screen
// =============================================================================
static void create_gameover_screen(void) {
    s_scr_gameover = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_gameover, lv_color_hex(0x0A0F1D), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_scr_gameover, LV_OPA_COVER, LV_PART_MAIN);

    // Title: GAME OVER
    lv_obj_t *title = lv_label_create(s_scr_gameover);
    lv_label_set_text(title, "GAME OVER!");
    lv_obj_set_style_text_color(title, lv_color_hex(0xEF4444), LV_PART_MAIN); // Red
    lv_obj_set_style_transform_zoom(title, 320, LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // Score Card
    lv_obj_t *card = lv_obj_create(s_scr_gameover);
    lv_obj_set_size(card, 280, 80);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x1E293B), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0x334155), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 12, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_final_score_label = lv_label_create(card);
    lv_label_set_text(s_final_score_label, "FINAL SCORE: 0");
    lv_obj_set_style_text_color(s_final_score_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(s_final_score_label, LV_ALIGN_TOP_MID, 0, 5);

    s_gameover_high_score_label = lv_label_create(card);
    lv_label_set_text(s_gameover_high_score_label, "BEST SCORE: 0");
    lv_obj_set_style_text_color(s_gameover_high_score_label, lv_color_hex(0xFBBF24), LV_PART_MAIN);
    lv_obj_align(s_gameover_high_score_label, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Button 1: PLAY AGAIN (Left)
    lv_obj_t *replay_btn = lv_btn_create(s_scr_gameover);
    lv_obj_set_size(replay_btn, 170, 60);
    lv_obj_align(replay_btn, LV_ALIGN_CENTER, -95, 80);
    lv_obj_set_style_radius(replay_btn, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(replay_btn, lv_color_hex(0x10B981), LV_PART_MAIN);
    lv_obj_add_event_cb(replay_btn, menu_start_btn_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *replay_lbl = lv_label_create(replay_btn);
    lv_label_set_text(replay_lbl, "PLAY AGAIN");
    lv_obj_set_style_text_color(replay_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(replay_lbl);

    // Button 2: MAIN MENU (Right)
    lv_obj_t *menu_btn = lv_btn_create(s_scr_gameover);
    lv_obj_set_size(menu_btn, 170, 60);
    lv_obj_align(menu_btn, LV_ALIGN_CENTER, 95, 80);
    lv_obj_set_style_radius(menu_btn, 14, LV_PART_MAIN);
    lv_obj_set_style_bg_color(menu_btn, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_add_event_cb(menu_btn, goto_menu_btn_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *menu_lbl = lv_label_create(menu_btn);
    lv_label_set_text(menu_lbl, "MAIN MENU");
    lv_obj_set_style_text_color(menu_lbl, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(menu_lbl);
}

// =============================================================================
// Process Commands from g_gui_cmd_queue (Executed on gui_task thread)
// =============================================================================
void simon_ui_process_cmds(void) {
    gui_cmd_t cmd;
    while (xQueueReceive(g_gui_cmd_queue, &cmd, 0) == pdTRUE) {
        switch (cmd.type) {
            case GUI_CMD_SHOW_SCREEN_MENU:
                ESP_LOGI(TAG, "GUI CMD: Load Menu Screen");
                lv_scr_load(s_scr_menu);
                break;

            case GUI_CMD_SHOW_SCREEN_GAME:
                ESP_LOGI(TAG, "GUI CMD: Load Game Screen");
                lv_scr_load(s_scr_game);
                break;

            case GUI_CMD_SHOW_SCREEN_GAMEOVER:
                ESP_LOGI(TAG, "GUI CMD: Load Game Over Screen");
                lv_scr_load(s_scr_gameover);
                break;

            case GUI_CMD_SET_PROMPT:
                if (s_prompt_label) {
                    lv_label_set_text(s_prompt_label, cmd.data.prompt.message);
                }
                break;

            case GUI_CMD_UPDATE_SCORE:
                if (s_score_label) {
                    lv_label_set_text_fmt(s_score_label, "SCORE: %d", cmd.data.score_info.score);
                }
                if (s_round_label) {
                    lv_label_set_text_fmt(s_round_label, "ROUND %d", cmd.data.score_info.round);
                }
                if (s_menu_high_score_label) {
                    lv_label_set_text_fmt(s_menu_high_score_label, "BEST SCORE: %d", cmd.data.score_info.high_score);
                }
                if (s_final_score_label) {
                    lv_label_set_text_fmt(s_final_score_label, "FINAL SCORE: %d", cmd.data.score_info.score);
                }
                if (s_gameover_high_score_label) {
                    lv_label_set_text_fmt(s_gameover_high_score_label, "BEST SCORE: %d", cmd.data.score_info.high_score);
                }
                break;

            case GUI_CMD_FLASH_PAD:
                simon_ui_set_pad_active(cmd.data.pad_state.pad, cmd.data.pad_state.active);
                break;

            case GUI_CMD_ENABLE_PADS:
                s_pads_enabled = cmd.data.pads_control.enabled;
                ESP_LOGI(TAG, "GUI CMD: Pads touch enabled = %d", s_pads_enabled);
                break;
        }
    }
}

// =============================================================================
// The FreeRTOS GUI Engine Task (Core 1)
// =============================================================================
static void gui_task(void *pvParameter) {
    ESP_LOGI(TAG, "GUI Task started on Core %d", xPortGetCoreID());

    uint32_t stats_timer = 0;

    while (1) {
        // 1. Process any incoming commands from game_logic_task
        simon_ui_process_cmds();

        // 2. Run LVGL rendering and input dispatch
        uint32_t delay_ms = lv_timer_handler();
        if (delay_ms < 5) delay_ms = 5;
        if (delay_ms > 20) delay_ms = 20;

        // 3. Educational RTOS Metric: Log stack high water mark periodically
        stats_timer += delay_ms;
        if (stats_timer >= 5000) {
            stats_timer = 0;
            UBaseType_t stack_remaining = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "[RTOS Metrics] gui_task Free Stack: %u words (%u bytes)",
                     (unsigned)stack_remaining, (unsigned)(stack_remaining * sizeof(StackType_t)));
        }

        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

// =============================================================================
// Initialization Entry Point
// =============================================================================
esp_err_t simon_ui_init(void) {
    ESP_LOGI(TAG, "Initializing Simon Says UI and IPC Queues...");

    // 1. Create FreeRTOS IPC Queues
    // user_input_queue: Capacity for 8 user touch events
    g_user_input_queue = xQueueCreate(8, sizeof(input_event_t));
    if (!g_user_input_queue) {
        ESP_LOGE(TAG, "Failed to create g_user_input_queue!");
        return ESP_ERR_NO_MEM;
    }

    // gui_cmd_queue: Capacity for 16 commands from game engine
    g_gui_cmd_queue = xQueueCreate(16, sizeof(gui_cmd_t));
    if (!g_gui_cmd_queue) {
        ESP_LOGE(TAG, "Failed to create g_gui_cmd_queue!");
        return ESP_ERR_NO_MEM;
    }

    // 2. Start hardware periodic timer for LVGL tick (2 ms)
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "periodic_gui_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 2000));

    // 3. Build the UI Screens
    create_menu_screen();
    create_game_screen();
    create_gameover_screen();

    // Start on the Main Menu screen & play cheerful startup song
    lv_scr_load(s_scr_menu);
    buzzer_play(SOUND_STARTUP);

    // 4. Spawn the GUI Task on Core 1 (APP CPU) with 8 KB Stack & Priority 5
    if (xTaskCreatePinnedToCore(gui_task, "gui_task", 8192, NULL, 5, NULL, 1) != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn gui_task!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Simon UI initialized successfully.");
    return ESP_OK;
}
