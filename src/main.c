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

void keypad_init_pins(void);
void keypad_init_timer(void);

#define PIN_SDI 11
#define PIN_CS 13
#define PIN_SCK 10
#define PIN_DC 14
#define PIN_nRESET 15

#define LOOP_DELAY_MS  20
#define CLIP_TICKS (2000 / LOOP_DELAY_MS)   // 2 seconds worth of 20ms ticks

#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF
#define COL_GREEN 0x07E0
#define COL_YELLOW 0xFFE0
#define COL_GRAY 0x4208
#define COL_CYAN 0x07FF
#define COL_MAGENTA 0xF81F
#define COL_ORANGE 0xFD20

typedef enum {
    STANDBY,
    CAPTURING,
    PLAYBACK
} BoxState;

static BoxState box_state = STANDBY;
static int tick_count = 0;
static char active_slot = 0;

void spi_lcd_bring_up() {
    gpio_set_function(PIN_CS, GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC, GPIO_FUNC_SIO);
    gpio_set_function(PIN_nRESET, GPIO_FUNC_SIO);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_set_dir(PIN_DC, GPIO_OUT);
    gpio_set_dir(PIN_nRESET, GPIO_OUT);
    gpio_put(PIN_CS, 1);
    gpio_put(PIN_DC,0);
    gpio_put(PIN_nRESET,0);
    sleep_ms(50);
    gpio_put(PIN_nRESET,1);
    sleep_ms(50);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SDI, GPIO_FUNC_SPI);
    spi_init(spi1,20000000);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
}

void ui_hint_row() {
    LCD_DrawFillRectangle(0, 210, 240, 240, COL_BLACK);
    LCD_DrawString(10, 215, COL_WHITE, COL_BLACK, "1-D:SEL *:REC 0:PLY", 20, 0);
}

void ui_slot_header(char slot) {
    LCD_DrawFillRectangle(0, 0, 240, 45, COL_BLACK);
    if (slot == 0) {
        LCD_DrawString(25, 12, COL_YELLOW, COL_BLACK, "SELECT A SLOT", 20, 0);
    } else {
        char tmp[20];
        sprintf(tmp, "SLOT: [%c]", slot);
        LCD_DrawString(50, 10, COL_MAGENTA, COL_BLACK, tmp, 24, 0);
    }
}

void ui_state_banner(BoxState st, char slot) {
    LCD_DrawFillRectangle(0, 60, 240, 140, COL_BLACK);
    char tmp[20];
    if (st == STANDBY) {
        LCD_DrawString(45, 90, COL_CYAN, COL_BLACK, "SYSTEM READY", 24, 0);
    } else if (st == CAPTURING) {
        sprintf(tmp, "REC TO [%c]", slot);
        LCD_DrawString(40, 90, COL_ORANGE, COL_BLACK, tmp, 24, 0);
    } else {
        sprintf(tmp, "PLAYING [%c]", slot);
        LCD_DrawString(35, 90, COL_GREEN, COL_BLACK, tmp, 24, 0);
    }
}

void ui_progress(int ticks_left) {
    if (ticks_left < 0) ticks_left = 0;
    int fill = (ticks_left * 200) / CLIP_TICKS;
    LCD_DrawFillRectangle(20,130, 20 + fill, 145, COL_CYAN);
    LCD_DrawFillRectangle(20 + fill, 130, 220,145, COL_GRAY);
}

void ui_clear_progress() {
    LCD_DrawFillRectangle(0, 125, 240, 150, COL_BLACK);
}

void ui_vol_strip(int pct) {
    char tmp[20];
    sprintf(tmp, "VOL: %d%%", pct);
    LCD_DrawString(10, 175, COL_WHITE, COL_BLACK, tmp, 16, 0);
    int fill = (pct * 140) / 100;
    LCD_DrawFillRectangle(85,178, 85 + fill, 188, COL_GREEN);
    LCD_DrawFillRectangle(85 + fill, 178, 225,188, COL_GRAY);
}

int main() {
    stdio_init_all();
    spi_lcd_bring_up();
    LCD_Setup();
    LCD_Clear(COL_BLACK);
    init_volume_adc();
    keypad_init_pins();
    keypad_init_timer();

    ui_hint_row();
    ui_slot_header(active_slot);
    ui_state_banner(STANDBY, active_slot);

    BoxState prev_state = STANDBY;
    char prev_slot = 0;

    while (1) {
        // volume knob
        uint16_t raw_knob = read_volume_raw();
        int vol_pct = (raw_knob * 100) / 4095;
        ui_vol_strip(vol_pct);

        // keypad
        uint16_t ev = 0;
        if (kev.head != kev.tail) ev = key_pop();

        if (ev != 0) {
            bool dn  = (ev >> 8) & 1;
            char key = (char)(ev & 0xFF);

            if (dn) {
                if ((key >= '1' && key <= '9') || (key >= 'A' && key <= 'D')) {
                    active_slot = key;
                } 
                else if (key == '*' && active_slot != 0 && box_state == STANDBY) {
                    box_state  = CAPTURING;
                    tick_count = 0;
                } 
                else if (key == '0' && active_slot != 0 && box_state == STANDBY) {
                    box_state  = PLAYBACK;
                    tick_count = 0;
                } 
                else if (key == '#') {
                    box_state = STANDBY;
                    ui_clear_progress();
                }
            }
        }

        // redraw only on change
        if (active_slot != prev_slot) {
            ui_slot_header(active_slot);
            prev_slot = active_slot;
        }
        if (box_state != prev_state) {
            ui_state_banner(box_state, active_slot);
            prev_state = box_state;
            if (box_state == STANDBY) ui_clear_progress();
        }

        // tick-based timer
        if (box_state != STANDBY) {
            tick_count++;
            ui_progress(CLIP_TICKS - tick_count);

            if (tick_count >= CLIP_TICKS) {
                box_state = STANDBY;
                tick_count = 0;
                ui_clear_progress();
            }
        }

        sleep_ms(LOOP_DELAY_MS);
    }
}