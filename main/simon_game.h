#ifndef SIMON_GAME_H
#define SIMON_GAME_H

#include "esp_err.h"

// Initializes Simon Says game logic engine and spawns game_logic_task
esp_err_t simon_game_init(void);

#endif // SIMON_GAME_H

