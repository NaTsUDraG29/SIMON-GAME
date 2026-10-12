#include "buzzer.h"
#include <stdio.h>
#include "esp_log.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BUZZER";

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_10_BIT // 0 to 1023
#define LEDC_DUTY_50            512               // 50% square wave
#define LEDC_FREQUENCY_INIT     2000              // Default 2 kHz

// Note Frequencies in Hertz (Hz)
#define NOTE_REST 0
#define NOTE_C4   262
#define NOTE_D4   294
#define NOTE_DS4  311
#define NOTE_E4   330
#define NOTE_F4   349
#define NOTE_G4   392
#define NOTE_A4   440
#define NOTE_B4   494
#define NOTE_C5   523
#define NOTE_CS5  554
#define NOTE_D5   587
#define NOTE_DS5  622
#define NOTE_E5   659
#define NOTE_FS5  740
#define NOTE_G5   784
#define NOTE_A5   880
#define NOTE_B5   988
#define NOTE_C6   1047

typedef struct {
    uint16_t freq_hz;
    uint16_t duration_ms;
} note_t;

// =============================================================================
// Melodies and Tunes
// =============================================================================

// 1. Startup Song (Menu loaded) - 6 cheerful ascending notes
static const note_t TUNE_STARTUP[] = {
    { NOTE_C4, 100 }, { NOTE_E4, 100 }, { NOTE_G4, 100 },
    { NOTE_C5, 200 }, { NOTE_REST, 60 }, { NOTE_G4, 100 }, { NOTE_C5, 300 },
    { 0, 0 }
};

// 2. Game Start Tune - Energetic rising arpeggio on clicking "START GAME"
static const note_t TUNE_GAME_START[] = {
    { NOTE_G4, 90 }, { NOTE_C5, 90 }, { NOTE_E5, 90 }, { NOTE_G5, 260 },
    { 0, 0 }
};

// 3. Simon Color Pad Tones (Distinct harmonic intervals)
static const note_t TUNE_PAD_GREEN[]  = { { NOTE_E4,  250 }, { 0, 0 } }; // Green: 330 Hz
static const note_t TUNE_PAD_RED[]    = { { NOTE_A4,  250 }, { 0, 0 } }; // Red:   440 Hz
static const note_t TUNE_PAD_YELLOW[] = { { NOTE_CS5, 250 }, { 0, 0 } }; // Yellow:554 Hz
static const note_t TUNE_PAD_BLUE[]   = { { NOTE_E5,  250 }, { 0, 0 } }; // Blue:  659 Hz

// 4. Correct Sequence (Round Victory Fanfare)
static const note_t TUNE_ROUND_SUCCESS[] = {
    { NOTE_C5, 100 }, { NOTE_E5, 100 }, { NOTE_G5, 120 }, { NOTE_C6, 350 },
    { 0, 0 }
};

// 5. Failed Sequence (Sad descending wah-wah-wah-wahhh)
static const note_t TUNE_GAME_OVER[] = {
    { NOTE_F4, 200 }, { NOTE_E4, 200 }, { NOTE_DS4, 200 }, { NOTE_D4, 450 },
    { 0, 0 }
};

static QueueHandle_t s_sound_queue = NULL;

// Helper: Set PWM output frequency & duty cycle
static void buzzer_set_freq(uint32_t freq_hz) {
    if (freq_hz == 0) {
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, 0);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    } else {
        ledc_set_freq(LEDC_MODE, LEDC_TIMER, freq_hz);
        ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, LEDC_DUTY_50);
        ledc_update_duty(LEDC_MODE, LEDC_CHANNEL);
    }
}

void buzzer_stop(void) {
    buzzer_set_freq(0);
}

// =============================================================================
// FreeRTOS Dedicated Sound Service Task (Core 0, Priority 3)
// =============================================================================
static void sound_task(void *pvParameters) {
    ESP_LOGI(TAG, "Buzzer Sound Task started on Core %d", xPortGetCoreID());

    sound_id_t sound;
    while (1) {
        // Block indefinitely until a sound event is queued
        if (xQueueReceive(s_sound_queue, &sound, portMAX_DELAY) == pdTRUE) {
            const note_t *tune = NULL;

            switch (sound) {
                case SOUND_STARTUP:       tune = TUNE_STARTUP; break;
                case SOUND_GAME_START:    tune = TUNE_GAME_START; break;
                case SOUND_PAD_GREEN:     tune = TUNE_PAD_GREEN; break;
                case SOUND_PAD_RED:       tune = TUNE_PAD_RED; break;
                case SOUND_PAD_YELLOW:    tune = TUNE_PAD_YELLOW; break;
                case SOUND_PAD_BLUE:      tune = TUNE_PAD_BLUE; break;
                case SOUND_ROUND_SUCCESS: tune = TUNE_ROUND_SUCCESS; break;
                case SOUND_GAME_OVER:     tune = TUNE_GAME_OVER; break;
                default: break;
            }

            if (tune) {
                for (int i = 0; tune[i].duration_ms > 0; i++) {
                    // If a new sound arrived in queue, stop current melody and switch
                    if (uxQueueMessagesWaiting(s_sound_queue) > 0) {
                        break;
                    }

                    if (tune[i].freq_hz > 0) {
                        buzzer_set_freq(tune[i].freq_hz);
                    } else {
                        buzzer_set_freq(0);
                    }

                    vTaskDelay(pdMS_TO_TICKS(tune[i].duration_ms));

                    // Articulation gap between notes (15ms)
                    buzzer_set_freq(0);
                    vTaskDelay(pdMS_TO_TICKS(15));
                }
                buzzer_set_freq(0);
            }
        }
    }
}

// =============================================================================
// Public API
// =============================================================================

void buzzer_play(sound_id_t sound) {
    if (s_sound_queue) {
        xQueueSend(s_sound_queue, &sound, 0);
    }
}

esp_err_t buzzer_init(void) {
    ESP_LOGI(TAG, "Configuring Buzzer on GPIO %d using LEDC PWM...", BUZZER_GPIO);

    // 1. Configure LEDC Timer
    ledc_timer_config_t timer_conf = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY_INIT,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    esp_err_t ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. Configure LEDC Channel on GPIO 12
    ledc_channel_config_t channel_conf = {
        .gpio_num   = BUZZER_GPIO,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CHANNEL,
        .intr_type  = LEDC_INTR_DISABLE,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0, // Silent initially
        .hpoint     = 0
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. Create Sound Queue (Depth: 8 items)
    s_sound_queue = xQueueCreate(8, sizeof(sound_id_t));
    if (!s_sound_queue) {
        ESP_LOGE(TAG, "Failed to create s_sound_queue!");
        return ESP_ERR_NO_MEM;
    }

    // 4. Spawn sound_task on Core 0 with 2KB stack and Priority 3
    if (xTaskCreatePinnedToCore(sound_task, "sound_task", 2048, NULL, 3, NULL, 0) != pdPASS) {
        ESP_LOGE(TAG, "Failed to spawn sound_task!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Buzzer subsystem initialized successfully.");
    return ESP_OK;
}
