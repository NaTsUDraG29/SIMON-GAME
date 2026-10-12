# ESP32 FreeRTOS Simon Says Game

An arcade-style **Simon Says** memory challenge game engineered from the ground up on the **ESP32** dual-core microcontroller using **ESP-IDF**, **LVGL v8**, and **FreeRTOS**.

The project features high-speed SPI DMA display rendering, capacitive multi-touch input, an asynchronous hardware PWM buzzer audio engine, and a decoupled multi-tasking architecture communicating over thread-safe FreeRTOS queues.

---

## 🎮 Features

* **Polished LVGL v8 GUI**: Dark slate theme, dynamic prompt banners, high score tracking, smooth pad highlights, and animated screen transitions on a 480×320 canvas.
* **Dual-Core FreeRTOS Architecture**:
  * **Core 1 (APP CPU)**: Dedicated 60 Hz display rendering and sub-millisecond touch input dispatch (`gui_task`, Priority 5).
  * **Core 0 (PRO CPU)**: Sequential game state machine (`game_logic_task`, Priority 4) and asynchronous audio engine (`sound_task`, Priority 3).
* **Decoupled Inter-Process Communication (IPC)**: Zero mutex lock contention; all cross-core communication is mediated via FreeRTOS Message Queues (`g_user_input_queue`, `g_gui_cmd_queue`, `s_sound_queue`).
* **Rich Audio Feedback**:
  * Ascending 6-note boot chime on menu load.
  * Energetic rising launch arpeggio on game start.
  * 4 distinct musical pad tones ($E4, A4, C\sharp5, E5$ harmonic chord).
  * 4-note victory fanfare on clearing a round.
  * Classic descending fail buzz on mistake or timeout.
* **Memory & Timing Diagnostics**: Live FreeRTOS task stack high-water mark reporting via `uxTaskGetStackHighWaterMark()`.

---

## 🛠️ Hardware Specifications

| Component | Specification | Interface | Driver / Controller |
| :--- | :--- | :--- | :--- |
| **MCU** | ESP32-WROVER-E (Dual-Core Xtensa LX6 @ 240 MHz, 520 KB SRAM + PSRAM) | On-board | FreeRTOS / ESP-IDF |
| **Display** | 3.5" / 4.0" TFT LCD (480 × 320 Landscape, 16-bit RGB565) | SPI2 (HSPI) + DMA @ 25 MHz | Sitronix **ST7796S** |
| **Touch Sensor** | 10-point Capacitive Touch Panel | I2C (400 kHz Fast Mode) | Goodix **GT911** |
| **I/O Expander** | 8-Bit I2C Port Expander | I2C (400 kHz Fast Mode) | TI **TCA9534** |
| **Audio** | Piezo Buzzer | LEDC Hardware PWM | ESP32 LEDC Driver |

---

## 📌 GPIO & Pinout Mapping

### 1. ESP32 Direct Pinout

| ESP32 GPIO | Function | Destination | Notes |
| :---: | :--- | :--- | :--- |
| **GPIO 4** | SPI MOSI | LCD Controller | High-speed DMA data |
| **GPIO 5** | SPI SCLK | LCD Controller | 25 MHz clock |
| **GPIO 2** | LCD D/C | LCD Controller | Data (High) / Command (Low) |
| **GPIO 26** | I2C SDA | TCA9534 & GT911 | Shared I2C bus (external pull-ups) |
| **GPIO 27** | I2C SCL | TCA9534 & GT911 | 400 kHz SCL clock |
| **GPIO 15** | Touch INT | GT911 INT Pin | Held LOW during boot to lock address `0x5D` |
| **GPIO 12** | Buzzer PWM | Piezo Buzzer | LEDC Timer 0, Channel 0 (10-bit resolution) |

### 2. TCA9534 I/O Expander Pin Mapping (Address `0x27`)

| Expander Pin | Function | Active Level | Description |
| :---: | :--- | :---: | :--- |
| **P1** | `CTP_RST` | Active LOW | Hardware reset for Goodix GT911 touch panel |
| **P2** | `LCD_RST` | Active LOW | Hardware reset for ST7796S LCD |
| **P4** | `LCD_CS` | Active LOW | SPI Chip Select for ST7796S LCD |
| **P5** | `LCD_BL` | Active HIGH | Backlight enable gate |
| **P6** | `LCD_PWR` | Active HIGH | Power rail switch for 3.3V LCD regulator |

---

## 📦 Prerequisites & Dependencies

1. **ESP-IDF**: Version **v5.0+** or **v6.x** (Tested and verified on **ESP-IDF v6.1**).
2. **ESP-IDF Component Manager**: Automatically installs required dependencies:
   * **LVGL**: `lvgl/lvgl: ^8.3` (Installs `v8.4.0`).
3. **Build Tools**:
   * CMake 3.16+
   * Ninja Build System
   * Python 3.10+ with ESP-IDF virtual environment

---

## 📂 Project Structure

```
ESP32_RTOS/
├── main/
│   ├── app_events.h        # Shared IPC event queue definitions and structs
│   ├── buzzer.c / .h       # LEDC PWM audio driver and asynchronous sound_task
│   ├── gt911_touch.c / .h  # GT911 capacitive touch driver & coordinate mapping
│   ├── lcd.c / .h          # ST7796S LCD initialization & LVGL SPI DMA flush callback
│   ├── main.c              # Application entry point & hardware bring-up sequence
│   ├── simon_game.c / .h   # Game logic state machine task (Core 0)
│   ├── simon_ui.c / .h     # LVGL UI screens, event handlers, and gui_task (Core 1)
│   ├── system_init.c / .h  # SPI and I2C peripheral bus master configuration
│   ├── tca9534.c / .h      # TCA9534 I2C I/O expander driver
│   ├── idf_component.yml   # ESP-IDF component manager manifest (LVGL v8)
│   └── CMakeLists.txt      # Component build script
├── CMakeLists.txt          # Top-level project CMake configuration
├── sdkconfig.defaults      # Pre-configured LVGL 16-bit color swap settings
└── README.md               # Project documentation
```

---

## 🚀 Build & Flash Instructions

### 1. Set Up the ESP-IDF Environment
Open your terminal (PowerShell or Bash) and export the ESP-IDF environment variables:

**Windows (PowerShell):**
```powershell
. $env:IDF_PATH\export.ps1
```

**Linux / macOS:**
```bash
. $IDF_PATH/export.sh
```

### 2. Configure the Project (Optional)
The project includes an [`sdkconfig.defaults`](file:///c:/C_notes/ESP32_Bringup/ESP32_RTOS/sdkconfig.defaults) file that sets the required LVGL 16-bit color swapping (`CONFIG_LV_COLOR_16_SWAP=y`). To inspect or modify additional settings:

```bash
idf.py menuconfig
```

### 3. Build the Firmware
Compile the project using Ninja:

```bash
idf.py build
```

### 4. Flash and Monitor
Connect your board via USB and run:

```bash
idf.py -p <COM_PORT> flash monitor
```
*(Replace `<COM_PORT>` with your serial device port, e.g., `COM3` on Windows or `/dev/ttyUSB0` on Linux).*

---

## 🕹️ How to Play

1. **Boot**: The board boots, initializes the display, and plays a musical boot chime on the **Main Menu**.
2. **Start**: Tap **START GAME** $\to$ the screen switches to the game board and plays the launch arpeggio.
3. **Simon's Turn**: Simon demonstrates a color sequence. Watch the pads flash and listen to each unique tone.
4. **Your Turn**: When the banner prompts **"YOUR TURN!"**, tap the matching color pads in the same sequence:
   * **Top-Left**: Green ($E4$)
   * **Top-Right**: Red ($A4$)
   * **Bottom-Left**: Yellow ($C\sharp5$)
   * **Bottom-Right**: Blue ($E5$)
5. **Score & Advance**: Clearing the sequence plays the victory fanfare, awards **+10 points**, updates your best score, and increases playback speed in the next round.
6. **Game Over**: Tapping the wrong pad or letting the 6-second timer expire triggers the fail buzz and transitions to the **Game Over** screen with options to replay or return to the menu.

---

## 📄 License

This project is licensed under the MIT License - feel free to use, modify, and build upon it!

