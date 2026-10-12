#ifndef APP_EVENTS_H
#define APP_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// The 4 Simon Color Pads
typedef enum {
    PAD_GREEN = 0,
    PAD_RED   = 1,
    PAD_YELLOW= 2,
    PAD_BLUE  = 3,
    PAD_COUNT = 4
} simon_pad_t;

// Inputs sent from GUI Task -> Game Logic Task
typedef enum {
    INPUT_START_GAME = 0,
    INPUT_PAD_CLICKED,
    INPUT_GOTO_MENU,
} input_event_type_t;

typedef struct {
    input_event_type_t type;
    simon_pad_t        pad; // Valid when type == INPUT_PAD_CLICKED
} input_event_t;

// Commands sent from Game Logic Task -> GUI Task
typedef enum {
    GUI_CMD_SHOW_SCREEN_MENU = 0,
    GUI_CMD_SHOW_SCREEN_GAME,
    GUI_CMD_SHOW_SCREEN_GAMEOVER,
    GUI_CMD_SET_PROMPT,
    GUI_CMD_UPDATE_SCORE,
    GUI_CMD_FLASH_PAD,
    GUI_CMD_ENABLE_PADS, // true = Player Turn, false = Simon Turn (disabled)
} gui_cmd_type_t;

typedef struct {
    gui_cmd_type_t type;
    union {
        struct {
            char message[32];
        } prompt;
        struct {
            uint16_t score;
            uint16_t round;
            uint16_t high_score;
        } score_info;
        struct {
            simon_pad_t pad;
            bool active; // true = bright highlight, false = dim idle
        } pad_state;
        struct {
            bool enabled;
        } pads_control;
    } data;
} gui_cmd_t;

// Global FreeRTOS IPC Queues
extern QueueHandle_t g_user_input_queue;
extern QueueHandle_t g_gui_cmd_queue;

#endif // APP_EVENTS_H

