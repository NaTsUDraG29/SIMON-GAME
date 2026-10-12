#ifndef SIMON_UI_H
#define SIMON_UI_H

#include "esp_err.h"
#include "app_events.h"

// Initializes LVGL GUI, screens, and spawns gui_task
esp_err_t simon_ui_init(void);

// Processes commands from g_gui_cmd_queue (called inside gui_task)
void simon_ui_process_cmds(void);

#endif // SIMON_UI_H

