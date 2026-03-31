#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "volume_adc.h"
#include "lcd.h"
#include "queue.h"

// --- PROTOTYPES ---
void keypad_init_pins(void);
void keypad_init_timer(void);

#define PIN_SDI    11
#define PIN_CS     13
#define PIN_SCK    10
#define PIN_DC     14
#define PIN_nRESET 15

typedef enum {
    MODE_IDLE,
    MODE_RECORDING,
    MODE_PLAYING
} AppMode;

static AppMode current_mode = MODE_IDLE;
static uint32_t mode_start_ms = 0;
static char selected_button = 0; 
#define CLIP_DURATION_MS 2000

// Colors
#define COL_BLACK   0x0000
#define COL_WHITE   0xFFFF
#define COL_GREEN   0x07E0
#define COL_YELLOW  0xFFE0
#define COL_GRAY    0x4208
#define COL_CYAN    0x07FF
#define COL_MAGENTA 0xF81F
#define COL_ORANGE  0xFD20

void init_spi_lcd() {
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_DC, 0);
    gpio_put(PIN_nRESET, 0);
    sleep_ms(50);
    gpio_put(PIN_nRESET, 1);
    sleep_ms(50);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi1, 20 * 1000 * 1000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

// ── UI Drawing Functions ────────────────────────────────

void draw_instructions() {
    // Bigger font (size 20) and moved to the very bottom
    LCD_DrawFillRectangle(0, 210, 240, 240, COL_BLACK);
    LCD_DrawString(10, 215, COL_WHITE, COL_BLACK, "1-D:SEL *:REC 0:PLY", 20, 0);
}

void draw_status_bar(char btn) {
    LCD_DrawFillRectangle(0, 0, 240, 45, COL_BLACK); 
    if (btn == 0) {
        LCD_DrawString(25, 12, COL_YELLOW, COL_BLACK, "SELECT A SLOT", 20, 0);
    } else {
        char buf[20];
        sprintf(buf, "SLOT: [%c]", btn);
        LCD_DrawString(50, 10, COL_MAGENTA, COL_BLACK, buf, 24, 0);
    }
}

void draw_mode_banner(AppMode mode, char btn) {
    // HUGE WIPE to kill hieroglyphics - expanded area
    LCD_DrawFillRectangle(0, 60, 240, 140, COL_BLACK);
    
    if (mode == MODE_IDLE) {
        // Size 24 is safe and big
        LCD_DrawString(45, 90, COL_CYAN, COL_BLACK, "SYSTEM READY", 24, 0);
    } else if (mode == MODE_RECORDING) {
        char buf[20];
        sprintf(buf, "REC TO [%c]", btn);
        LCD_DrawString(40, 90, COL_ORANGE, COL_BLACK, buf, 24, 0);
    } else if (mode == MODE_PLAYING) {
        char buf[20];
        sprintf(buf, "PLAYING [%c]", btn);
        LCD_DrawString(35, 90, COL_GREEN, COL_BLACK, buf, 24, 0);
    }
}

void draw_progress_bar(uint32_t elapsed_ms) {
    int remaining = CLIP_DURATION_MS - (int)elapsed_ms;
    if (remaining < 0) remaining = 0;
    
    // Positioned right under the mode text
    int bar_width = (remaining * 200) / CLIP_DURATION_MS;
    LCD_DrawFillRectangle(20, 130, 20 + bar_width, 145, COL_CYAN);
    LCD_DrawFillRectangle(20 + bar_width, 130, 220, 145, COL_GRAY);
}

void clear_progress_bar() {
    LCD_DrawFillRectangle(0, 125, 240, 150, COL_BLACK);
}

void draw_volume(uint16_t adc_val, int volume) {
    char buffer[20];
    // Volume bar placed in the "bottom middle" area
    sprintf(buffer, "VOL: %d%%", volume);
    LCD_DrawString(10, 175, COL_WHITE, COL_BLACK, buffer, 16, 0);

    int bar_width = (volume * 140) / 100;
    LCD_DrawFillRectangle(85, 178, 85 + bar_width, 188, COL_GREEN);
    LCD_DrawFillRectangle(85 + bar_width, 178, 225, 188, COL_GRAY);
}

// ── Main ────────────────────────────────────────────────
int main() {
    stdio_init_all();
    init_spi_lcd();
    LCD_Setup();
    LCD_Clear(COL_BLACK);
    init_volume_adc();
    keypad_init_pins();
    keypad_init_timer();

    draw_instructions();
    draw_status_bar(selected_button);
    draw_mode_banner(MODE_IDLE, selected_button);

    AppMode last_mode = MODE_IDLE;
    char last_selected = 0;

    while (1) {
        // 1. Update Volume
        uint16_t adc_val = read_volume_raw();
        int volume = (adc_val * 100) / 4095;
        draw_volume(adc_val, volume);

        // 2. Keypad Poll
        uint16_t event = 0;
        if (kev.head != kev.tail) event = key_pop();

        if (event != 0) {
            bool pressed = (event >> 8) & 1;
            char key = (char)(event & 0xFF);

            if (pressed) {
                if ((key >= '1' && key <= '9') || (key >= 'A' && key <= 'D')) {
                    selected_button = key;
                }
                else if (key == '*' && selected_button != 0 && current_mode == MODE_IDLE) {
                    current_mode = MODE_RECORDING;
                    mode_start_ms = to_ms_since_boot(get_absolute_time());
                }
                else if (key == '0' && selected_button != 0 && current_mode == MODE_IDLE) {
                    current_mode = MODE_PLAYING;
                    mode_start_ms = to_ms_since_boot(get_absolute_time());
                }
                else if (key == '#') {
                    current_mode = MODE_IDLE;
                    clear_progress_bar();
                }
            }
        }

        // 3. UI Sync
        if (selected_button != last_selected) {
            draw_status_bar(selected_button);
            last_selected = selected_button;
        }

        if (current_mode != last_mode) {
            draw_mode_banner(current_mode, selected_button);
            last_mode = current_mode;
            if (current_mode == MODE_IDLE) clear_progress_bar();
        }

        // 4. Timer Logic
        if (current_mode != MODE_IDLE) {
            uint32_t now = to_ms_since_boot(get_absolute_time());
            uint32_t elapsed = now - mode_start_ms;
            
            draw_progress_bar(elapsed);

            if (elapsed >= CLIP_DURATION_MS) {
                current_mode = MODE_IDLE;
                clear_progress_bar();
            }
        }

        sleep_ms(20); 
    }
}