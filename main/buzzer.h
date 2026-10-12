#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>
#include "esp_err.h"

// Buzzer Hardware Pin
#define BUZZER_GPIO 12

// Sound Event IDs
typedef enum {
    SOUND_NONE = 0,
    SOUND_STARTUP,       // Cheerful startup song prompting menu loaded
    SOUND_GAME_START,    // Energetic rising tune on clicking "START GAME"
    SOUND_PAD_GREEN,     // Pad Green tone (E4 - 330 Hz)
    SOUND_PAD_RED,       // Pad Red tone (A4 - 440 Hz)
    SOUND_PAD_YELLOW,    // Pad Yellow tone (C#5 - 554 Hz)
    SOUND_PAD_BLUE,      // Pad Blue tone (E5 - 659 Hz)
    SOUND_ROUND_SUCCESS, // Triumphant victory fanfare on clearing a round
    SOUND_GAME_OVER,     // Sad descending buzz on mistake
} sound_id_t;

// Initialize the LEDC PWM hardware on GPIO 12 and launch sound_task
esp_err_t buzzer_init(void);

// Non-blocking, thread-safe sound dispatch. Safe to call from ANY task or core!
void buzzer_play(sound_id_t sound);

// Stop any currently playing sound
void buzzer_stop(void);

#endif // BUZZER_H

