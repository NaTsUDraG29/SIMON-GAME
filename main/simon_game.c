#include "simon_game.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "app_events.h"
#include "buzzer.h"

static const char *TAG = "SIMON_GAME";

#define MAX_SEQUENCE_LEN 64

typedef enum {
    STATE_MENU = 0,
    STATE_START_ROUND,
    STATE_SIMON_DEMO,
    STATE_PLAYER_TURN,
    STATE_ROUND_SUCCESS,
    STATE_GAME_OVER
} game_state_t;

// Game State Variables
static simon_pad_t s_sequence[MAX_SEQUENCE_LEN];
static uint16_t    s_sequence_len = 0;
static uint16_t    s_current_score = 0;
static uint16_t    s_current_round = 1;
static uint16_t    s_high_score = 0;

// Helper: Send a GUI command into g_gui_cmd_queue
static void send_gui_cmd(const gui_cmd_t *cmd) {
    if (g_gui_cmd_queue) {
        xQueueSend(g_gui_cmd_queue, cmd, pdMS_TO_TICKS(50));
    }
}

static void send_prompt(const char *msg) {
    gui_cmd_t cmd = {
        .type = GUI_CMD_SET_PROMPT
    };
    strncpy(cmd.data.prompt.message, msg, sizeof(cmd.data.prompt.message) - 1);
    send_gui_cmd(&cmd);
}

static void send_score_update(void) {
    gui_cmd_t cmd = {
        .type = GUI_CMD_UPDATE_SCORE,
        .data.score_info = {
            .score = s_current_score,
            .round = s_current_round,
            .high_score = s_high_score
        }
    };
    send_gui_cmd(&cmd);
}

static void set_pads_enabled(bool enabled) {
    gui_cmd_t cmd = {
        .type = GUI_CMD_ENABLE_PADS,
        .data.pads_control = { .enabled = enabled }
    };
    send_gui_cmd(&cmd);
}

static void flash_pad(simon_pad_t pad, bool active) {
    gui_cmd_t cmd = {
        .type = GUI_CMD_FLASH_PAD,
        .data.pad_state = {
            .pad = pad,
            .active = active
        }
    };
    send_gui_cmd(&cmd);
}

// =============================================================================
// The FreeRTOS Game Logic Engine Task (Core 0)
// =============================================================================
static void game_logic_task(void *pvParameters) {
    ESP_LOGI(TAG, "Game Engine Task started on Core %d", xPortGetCoreID());

    game_state_t state = STATE_MENU;
    input_event_t in_ev;

    // Initial broadcast to display Menu with high score
    gui_cmd_t scr_cmd = { .type = GUI_CMD_SHOW_SCREEN_MENU };
    send_gui_cmd(&scr_cmd);
    send_score_update();

    while (1) {
        // Log task stack usage on state entry
        UBaseType_t stack_free = uxTaskGetStackHighWaterMark(NULL);
        ESP_LOGD(TAG, "[RTOS Metrics] game_task Free Stack: %u words (%u bytes)",
                 (unsigned)stack_free, (unsigned)(stack_free * sizeof(StackType_t)));

        switch (state) {
            // -----------------------------------------------------------------
            // STATE: MAIN MENU (Idle wait for Start button)
            // -----------------------------------------------------------------
            case STATE_MENU: {
                ESP_LOGI(TAG, "Game State: MENU. Waiting for Start Game click...");
                set_pads_enabled(false);
                send_score_update();

                // Block indefinitely until an input event arrives
                if (xQueueReceive(g_user_input_queue, &in_ev, portMAX_DELAY) == pdTRUE) {
                    if (in_ev.type == INPUT_START_GAME) {
                        ESP_LOGI(TAG, "Input Received: START GAME! Initializing game...");
                        s_current_score = 0;
                        s_current_round = 1;
                        s_sequence_len = 0;

                        // Switch to game screen
                        gui_cmd_t cmd_game = { .type = GUI_CMD_SHOW_SCREEN_GAME };
                        send_gui_cmd(&cmd_game);
                        send_score_update();

                        state = STATE_START_ROUND;
                    }
                }
                break;
            }

            // -----------------------------------------------------------------
            // STATE: START ROUND (Add new color step, update UI)
            // -----------------------------------------------------------------
            case STATE_START_ROUND: {
                ESP_LOGI(TAG, "Game State: Starting Round %d (Score: %d)", s_current_round, s_current_score);
                set_pads_enabled(false);
                send_score_update();
                send_prompt("GET READY...");

                // Add next random pad (0 to 3) to sequence
                if (s_sequence_len < MAX_SEQUENCE_LEN) {
                    s_sequence[s_sequence_len++] = (simon_pad_t)(esp_random() % PAD_COUNT);
                }

                // Log the full sequence to console for debugging
                ESP_LOGI(TAG, "Generated Sequence Length: %d. Latest step: %d", s_sequence_len, s_sequence[s_sequence_len - 1]);

                vTaskDelay(pdMS_TO_TICKS(800)); // Dramatic pause before demo starts
                state = STATE_SIMON_DEMO;
                break;
            }

            // -----------------------------------------------------------------
            // STATE: SIMON DEMO (Playback sequence with precise FreeRTOS delays)
            // -----------------------------------------------------------------
            case STATE_SIMON_DEMO: {
                ESP_LOGI(TAG, "Game State: Simon Demo playback...");
                send_prompt("WATCH SIMON...");
                set_pads_enabled(false);

                // Drain any accidental leftover input events before playback
                xQueueReset(g_user_input_queue);

                // Playback speed dynamically scales with round
                uint32_t on_time = 450;
                uint32_t gap_time = 200;
                if (s_current_round > 5)  { on_time = 350; gap_time = 150; }
                if (s_current_round > 10) { on_time = 250; gap_time = 100; }

                bool aborted = false;
                for (int i = 0; i < s_sequence_len; i++) {
                    // Check if player tapped MENU button to abort
                    if (xQueueReceive(g_user_input_queue, &in_ev, 0) == pdTRUE) {
                        if (in_ev.type == INPUT_GOTO_MENU) {
                            aborted = true;
                            break;
                        }
                    }

                    // 1. Turn ON pad & play its color tone
                    flash_pad(s_sequence[i], true);
                    buzzer_play((sound_id_t)(SOUND_PAD_GREEN + s_sequence[i]));
                    vTaskDelay(pdMS_TO_TICKS(on_time));

                    // 2. Turn OFF pad & stop buzzer
                    flash_pad(s_sequence[i], false);
                    buzzer_stop();
                    vTaskDelay(pdMS_TO_TICKS(gap_time));
                }

                if (aborted) {
                    state = STATE_GAME_OVER;
                    // gui_cmd_t cmd_menu = { .type = GUI_CMD_SHOW_SCREEN_GAMEOVER };
                    // send_gui_cmd(&cmd_menu);
                    
                } else {
                    state = STATE_PLAYER_TURN;
                }
                break;
            }

            // -----------------------------------------------------------------
            // STATE: PLAYER TURN (Wait and verify each player tap)
            // -----------------------------------------------------------------
            case STATE_PLAYER_TURN: {
                ESP_LOGI(TAG, "Game State: Player Turn!");
                send_prompt("YOUR TURN!");
                set_pads_enabled(true);

                // Clear input queue so player starts with clean slate
                xQueueReset(g_user_input_queue);

                bool round_failed = false;

                for (int step = 0; step < s_sequence_len; step++) {
                    // Wait for player tap with a 6-second timeout
                    if (xQueueReceive(g_user_input_queue, &in_ev, pdMS_TO_TICKS(6000)) == pdTRUE) {
                        if (in_ev.type == INPUT_GOTO_MENU) {
                            // Player hit < MENU back button
                            // gui_cmd_t cmd_menu = { .type = GUI_CMD_SHOW_SCREEN_MENU };
                            // send_gui_cmd(&cmd_menu);
                            // state = STATE_MENU;
                            round_failed = true;
                            break;
                        }

                        if (in_ev.type == INPUT_PAD_CLICKED) {
                            ESP_LOGI(TAG, "Step %d/%d: Player tapped %d, Expected %d",
                                     step + 1, s_sequence_len, in_ev.pad, s_sequence[step]);

                            if (in_ev.pad == s_sequence[step]) {
                                // Correct pad! Brief pause, then reset pad to dim
                                vTaskDelay(pdMS_TO_TICKS(180));
                                flash_pad(in_ev.pad, false);
                            } else {
                                // Wrong pad! Game Over!
                                ESP_LOGW(TAG, "INCORRECT PAD! Game Over!");
                                flash_pad(in_ev.pad, false);
                                round_failed = true;
                                break;
                            }
                        }
                    } else {
                        // Timeout expired! Player took too long
                        ESP_LOGW(TAG, "TIMEOUT! Player did not respond in 6 seconds.");
                        send_prompt("TOO SLOW!");
                        round_failed = true;
                        break;
                    }
                }

                set_pads_enabled(false);

                if (round_failed) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    state = STATE_GAME_OVER;
                } else {
                    state = STATE_ROUND_SUCCESS;
                }
                break;
            }

            // -----------------------------------------------------------------
            // STATE: ROUND SUCCESS (Award points, celebrate, advance)
            // -----------------------------------------------------------------
            case STATE_ROUND_SUCCESS: {
                ESP_LOGI(TAG, "Round %d Complete! Awarding points...", s_current_round);
                s_current_score += 10;
                s_current_round++;

                if (s_current_score > s_high_score) {
                    s_high_score = s_current_score;
                }

                send_score_update();
                send_prompt("CORRECT! +10 PTS");
                buzzer_play(SOUND_ROUND_SUCCESS);
                vTaskDelay(pdMS_TO_TICKS(1000));

                state = STATE_START_ROUND;
                break;
            }

            // -----------------------------------------------------------------
            // STATE: GAME OVER (Show summary, wait for replay or menu)
            // -----------------------------------------------------------------
            case STATE_GAME_OVER: {
                ESP_LOGI(TAG, "Game State: GAME OVER. Final Score: %d, Best: %d",
                         s_current_score, s_high_score);
                send_score_update();
                buzzer_play(SOUND_GAME_OVER);

                // Switch to game over screen
                gui_cmd_t cmd_over = { .type = GUI_CMD_SHOW_SCREEN_GAMEOVER };
                send_gui_cmd(&cmd_over);

                // Wait for Play Again or Main Menu
                if (xQueueReceive(g_user_input_queue, &in_ev, portMAX_DELAY) == pdTRUE) {
                    if (in_ev.type == INPUT_START_GAME) {
                        ESP_LOGI(TAG, "Play Again requested!");
                        s_current_score = 0;
                        s_current_round = 1;
                        s_sequence_len = 0;

                        gui_cmd_t cmd_game = { .type = GUI_CMD_SHOW_SCREEN_GAME };
                        send_gui_cmd(&cmd_game);
                        send_score_update();

                        state = STATE_START_ROUND;
                    } else if (in_ev.type == INPUT_GOTO_MENU) {
                        ESP_LOGI(TAG, "Return to Main Menu requested!");
                        gui_cmd_t cmd_menu = { .type = GUI_CMD_SHOW_SCREEN_MENU };
                        send_gui_cmd(&cmd_menu);
                        state = STATE_MENU;
                    }
                }
                break;
            }
        }
    }
}

// =============================================================================
// Initialization Entry Point
// =============================================================================
esp_err_t simon_game_init(void) {
    ESP_LOGI(TAG, "Spawning Simon Says Game Logic Engine Task...");

    // Spawn game_logic_task on Core 0 (PRO CPU) with 4 KB Stack & Priority 4
    if (xTaskCreatePinnedToCore(game_logic_task, "simon_game", 4096, NULL, 4, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn game_logic_task!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Game Logic Engine Task initialized successfully.");
    return ESP_OK;
}

