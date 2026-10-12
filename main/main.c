#include <stdio.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_rom_sys.h" // Needed for esp_rom_delay_us()
#include "esp_heap_caps.h" // Required for DMA memory allocation
#include "driver/spi_master.h"

#include "system_init.h"
#include "gt911_touch.h"
#include "tca9534.h"
#include "lcd.h"
#include "simon_ui.h"
#include "simon_game.h"
#include "buzzer.h"

static const char *TAG = "MAIN_APP";


void app_main(void) {
    ESP_LOGI(TAG, "System Booting...");
    
    //initilaise the system protocols and GPIOs
    if(System_Init() != ESP_OK) {
        ESP_LOGE(TAG, "System Initialization Failed!");
    } else {
        ESP_LOGI(TAG, "System Initialized Successfully!");
    }

    //Initilaise the TCA9534 IO explander
    if(TCA9534_Init() != ESP_OK) {
        ESP_LOGE(TAG, "TCA9534 Initialization Failed!");  
    } else {    
        ESP_LOGI(TAG, "TCA9534 Initialized Successfully!");
    }

    // Initialise the LCD
    if(lcd_lvgl_init() != ESP_OK) {  
        ESP_LOGE(TAG, "LCD Initialization Failed!");  
    } else {    
        ESP_LOGI(TAG, "LCD Initialized Successfully!");
    }  
           
    //Initilaise the GT911 touch controller
    if(gt911_init() != ESP_OK) {
        ESP_LOGE(TAG, "GT911 Initialization Failed!");  
    } else {    
        ESP_LOGI(TAG, "GT911 Initialized Successfully!");
    }

    // 5. Initialize Buzzer Audio Subsystem (LEDC PWM on GPIO 12 & sound_task)
    if(buzzer_init() != ESP_OK) {
        ESP_LOGE(TAG, "Buzzer Initialization Failed!");
    } else {
        ESP_LOGI(TAG, "Buzzer Initialized Successfully!");
    }

    // 6. Initialize Simon UI (Screens, GUI Task on Core 1)
    if(simon_ui_init() != ESP_OK) {  
        ESP_LOGE(TAG, "Simon UI Initialization Failed!");  
    } else {    
        ESP_LOGI(TAG, "Simon UI Initialized Successfully!");
    }   

    // 7. Initialize Simon Game Logic Engine (State Machine, Game Task on Core 0)
    if(simon_game_init() != ESP_OK) {  
        ESP_LOGE(TAG, "Simon Game Engine Initialization Failed!");  
    } else {    
        ESP_LOGI(TAG, "Simon Game Engine Initialized Successfully!");
    }
}