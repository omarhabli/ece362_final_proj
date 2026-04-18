#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "sd_card.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"
#include "volume_adc.h"
#include "lcd.h"
#include "queue.h"
#include "ff.h"
#include "diskio.h"
#include "mic.h"
#include "i2s_rx_master.pio.h"

// MAY WANT TO ADD SOME ANIMATION WHILE LOADING TO AND
// FROM SD CARD

// REMEMBER TO GET RID OF ALL PRINTS

// -- SD Card Pin Definitions (SPI0) --
#define SD_MISO 16
#define SD_CS 17
#define SD_SCK 18
#define SD_MOSI 19

// -- LCD Pin Definitions (SPI1) --
#define PIN_LCD_SCK 10
#define PIN_LCD_MOSI 11
#define PIN_LCD_CS 13
#define PIN_LCD_DC 14
#define PIN_LCD_nRESET 15
#define AUDIO_SAMPLE_RATE 16000

// -- Timing & Audio Constants --
#define LOOP_DELAY_MS 20
#define CLIP_TICKS (1000 / LOOP_DELAY_MS)
#define SAMPLE_RATE 48000
#define CLIP_SAMPLES (SAMPLE_RATE * 1)

// -- Colors --
#define COL_BLACK 0x0000
#define COL_WHITE 0xFFFF
#define COL_GREEN 0x07E0
#define COL_YELLOW 0xFFE0
#define COL_GRAY 0x4208
#define COL_CYAN 0x07FF
#define COL_MAGENTA 0xF81F
#define COL_ORANGE 0xFD20

// -- PIO mic pins --
#define PIO_SCK 20
#define PIO_WS 21
#define PIO_DO 22

typedef enum { STANDBY, CAPTURING, PLAYBACK } BoxState;

static uint32_t audio_buf_len = 0;
static BoxState box_state = STANDBY;
static int tick_count = 0;
static char active_slot = 0;
static FATFS fat_vol;
static bool sd_ready = false;
static volatile uint32_t playback_pos = 0;
static volatile bool playing = false;
static volatile int vol_pct_global = 100;

// mic / audio buffers
static i2s_rx_master_t mic_ctx;
static uint32_t mic_raw_buf[CLIP_SAMPLES];
static int16_t audio_buf[CLIP_SAMPLES];
extern volatile bool recording_done;

void print_audio_buffer() {
    printf("---- PRINTING BUFFER ----\n");

    for (int i = 0; i < 200; i++) {
        printf("%d\n", audio_buf[i]);
    }

    printf("---- DONE ----\n");
}

// PWM playback
void pwm_playback_handler() {
    uint s0 = pwm_gpio_to_slice_num(36);
    pwm_clear_irq(s0);

    if (!playing) return;

    if (playback_pos >= audio_buf_len) {
        playing = false;
        playback_pos = 0;
        pwm_set_gpio_level(36, 0);
        return;
    }

    uint32_t period = pwm_hw->slice[s0].top;
    int32_t samp = audio_buf[playback_pos++];
    samp = (samp * vol_pct_global) / 100;
    uint32_t level = ((samp + 32768) * period) / 65535;
    pwm_set_gpio_level(36, level);
}

void init_pwm_audio_out() {
    gpio_set_function(36, GPIO_FUNC_PWM);
    uint s0 = pwm_gpio_to_slice_num(36);
    uint c0 = pwm_gpio_to_channel(36);

    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_clkdiv(&cfg, 125.0f);
    pwm_config_set_wrap(&cfg, (1000000 / AUDIO_SAMPLE_RATE) - 1);
    pwm_set_chan_level(s0, c0, 0);
    pwm_set_irq0_enabled(s0, true);
    irq_set_exclusive_handler(PWM_DEFAULT_IRQ_NUM(), pwm_playback_handler);
    irq_set_enabled(PWM_DEFAULT_IRQ_NUM(), true);
    pwm_init(s0, &cfg, true);
}

void start_playback() {
    playback_pos = 0;
    playing = true;
}

// -- UART Driver Implementation --
void init_uart() {
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    uart_init(uart0, 115200);
    uart_get_hw(uart0)->lcr_h = (3 << 5) | (1 << 4);
}

int _write(__unused int handle, char *buffer, int length) {
    int i = 0;
    while (i < length && buffer[i] != '\0') {
        uart_write_blocking(uart0, (uint8_t *)&buffer[i], 1);
        i++;
    }
    return length;
}

// -- Forward Declarations --
void keypad_init_pins(void);
void keypad_init_timer(void);

void sd_mount() {
    sleep_ms(2000);

    spi_deinit(spi0);
    sleep_ms(10);

    spi_init(spi0, 400 * 1000);

    gpio_set_function(SD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_MISO, GPIO_FUNC_SPI);

    gpio_init(SD_CS);
    gpio_set_dir(SD_CS, GPIO_OUT);
    gpio_put(SD_CS, 1);

    printf("Hardware reset. Attempting mount...\n");
    sleep_ms(100);

    FRESULT fr = f_mount(&fat_vol, "", 1);

    if (fr != FR_OK) {
        printf("CRITICAL: Mount failed with error %d\n", fr);
    } else {
        printf("MOUNT SUCCESS\n");
        sd_ready = true;
        spi_set_baudrate(spi0, 12 * 1000 * 1000);
    }
}

// -- SD File Operations --
static void slot_filename(char slot, char *out) {
    sprintf(out, "S%c.RAW", slot);
}

void sd_save_clip(char slot) {
    if (!sd_ready) {
        printf("SAVE FAIL: SD not ready\r\n");
        return;
    }

    char fname[16];
    slot_filename(slot, fname);
    FIL fil;
    UINT bw;
    FRESULT fr = f_open(&fil, fname, FA_WRITE | FA_CREATE_ALWAYS);
    if (fr == FR_OK) {
        f_write(&fil, audio_buf, audio_buf_len * sizeof(int16_t), &bw);
        f_close(&fil);
        printf("FILE WRITTEN: %s (%u bytes)\r\n", fname, bw);
    } else {
        printf("FILE OPEN ERROR (SAVE): %d\r\n", fr);
    }
}

uint32_t sd_load_clip(char slot) {
    if (!sd_ready) {
        printf("LOAD FAIL: SD not ready\r\n");
        return 0;
    }

    char fname[16];
    slot_filename(slot, fname);
    FIL fil;
    UINT br;
    FRESULT fr = f_open(&fil, fname, FA_READ);
    if (fr != FR_OK) {
        printf("FILE OPEN ERROR (LOAD): %d for file %s\r\n", fr, fname);
        return 0;
    }

    f_read(&fil, audio_buf, CLIP_SAMPLES * sizeof(int16_t), &br);
    f_close(&fil);
    return br / sizeof(int16_t);
}

// -- LCD Implementation --
void spi_lcd_bring_up() {
    gpio_init(PIN_LCD_CS);
    gpio_init(PIN_LCD_DC);
    gpio_init(PIN_LCD_nRESET);
    gpio_set_dir(PIN_LCD_CS, GPIO_OUT);
    gpio_set_dir(PIN_LCD_DC, GPIO_OUT);
    gpio_set_dir(PIN_LCD_nRESET, GPIO_OUT);

    gpio_put(PIN_LCD_CS, 1);
    gpio_put(PIN_LCD_DC, 0);
    gpio_put(PIN_LCD_nRESET, 0);
    sleep_ms(50);
    gpio_put(PIN_LCD_nRESET, 1);
    sleep_ms(50);

    spi_init(spi1, 20 * 1000 * 1000);
    gpio_set_function(PIN_LCD_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_LCD_MOSI, GPIO_FUNC_SPI);
    spi_set_format(spi1, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    printf("Hardware: SPI1 initialized for LCD (GP10-15)\r\n");
}

// -- UI Rendering Functions --
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
    LCD_DrawFillRectangle(20, 130, 20 + fill, 145, COL_CYAN);
    LCD_DrawFillRectangle(20 + fill, 130, 220, 145, COL_GRAY);
}

void ui_clear_progress() {
    LCD_DrawFillRectangle(0, 125, 240, 150, COL_BLACK);
}

void ui_vol_strip(int pct) {
    char tmp[20];
    sprintf(tmp, "VOL: %d%%", pct);
    LCD_DrawString(10, 175, COL_WHITE, COL_BLACK, tmp, 16, 0);
    int fill = (pct * 140) / 100;
    LCD_DrawFillRectangle(85, 178, 85 + fill, 188, COL_GREEN);
    LCD_DrawFillRectangle(85 + fill, 178, 225, 188, COL_GRAY);
}

// -- Main Execution Loop --
int main() {
    init_pwm_audio_out();

    init_uart();
    setbuf(stdout, NULL);

    spi_lcd_bring_up();
    LCD_Setup();
    LCD_Clear(COL_BLACK);

    init_volume_adc();
    keypad_init_pins();
    keypad_init_timer();

    init_sdcard_io();
    sd_mount();

    mic_init(
        &mic_ctx,
        pio0,
        0,
        PIO_SCK,
        PIO_WS,
        PIO_DO,
        SAMPLE_RATE,
        32
    );

    ui_hint_row();
    ui_slot_header(active_slot);
    ui_state_banner(STANDBY, active_slot);

    BoxState prev_state = STANDBY;
    char prev_slot = 0;

    while (1) {
        uint16_t raw_knob = read_volume_raw();
        int vol_pct = (raw_knob * 100) / 4095;
        vol_pct_global = vol_pct;
        ui_vol_strip(vol_pct);

        uint16_t ev = 0;
        if (kev.head != kev.tail) ev = key_pop();

        if (ev != 0) {
            bool dn = (ev >> 8) & 1;
            char key = (char)(ev & 0xFF);
            if (dn) {
                if ((key >= '1' && key <= '9') || (key >= 'A' && key <= 'D')) {
                    active_slot = key;
                    printf("\r\n--- Slot selected: %c ---\r\n", active_slot);
                } else if (key == '*' && active_slot != 0 && box_state == STANDBY) {
                    audio_buf_len = 0;
                    recording_done = false;
                    memset(mic_raw_buf, 0, sizeof(mic_raw_buf));
                    memset(audio_buf, 0, sizeof(audio_buf));
                    mic_start_capture(&mic_ctx, mic_raw_buf, CLIP_SAMPLES);
                    box_state = CAPTURING;
                    tick_count = 0;
                    printf("Action: Recording to S%c.RAW...\r\n", active_slot);
                } else if (key == '0' && active_slot != 0 && box_state == STANDBY) {
                    uint32_t loaded = sd_load_clip(active_slot);
                    if (loaded > 0) {
                        audio_buf_len = loaded;
                        printf("SUCCESS: Loaded %lu samples from S%c.RAW\r\n", (unsigned long)audio_buf_len, active_slot);
                        box_state = PLAYBACK;
                        tick_count = 0;
                        start_playback();
                    } else {
                        printf("ERROR: Could not load S%c.RAW\r\n", active_slot);
                    }
                } else if (key == '#') {
                    if (box_state == CAPTURING) {
                        mic_stop_capture(&mic_ctx);
                    }
                    playing = false;
                    playback_pos = 0;
                    box_state = STANDBY;
                    tick_count = 0;
                    ui_clear_progress();
                }
            }
        }

        if (active_slot != prev_slot) {
            ui_slot_header(active_slot);
            prev_slot = active_slot;
        }
        if (box_state != prev_state) {
            ui_state_banner(box_state, active_slot);
            prev_state = box_state;
            if (box_state == STANDBY) ui_clear_progress();
        }

        if (box_state == CAPTURING) {
            if (tick_count < CLIP_TICKS) tick_count++;
            ui_progress(CLIP_TICKS - tick_count);
        } else if (box_state == PLAYBACK) {
            if (tick_count < CLIP_TICKS) tick_count++;
            ui_progress(CLIP_TICKS - tick_count);
        }

        if (box_state == CAPTURING && recording_done) {
            recording_done = false;

            for (uint32_t i = 0; i < CLIP_SAMPLES; i++) {
                int32_t s = (int32_t)(mic_raw_buf[i] << 8) >> 8;
                audio_buf[i] = (int16_t)(s >> 8);
            }

            audio_buf_len = CLIP_SAMPLES;
            sd_save_clip(active_slot);

            box_state = STANDBY;
            tick_count = 0;
            ui_clear_progress();
        }

        if (box_state == PLAYBACK && !playing) {
            box_state = STANDBY;
            tick_count = 0;
            ui_clear_progress();
        }

        sleep_ms(LOOP_DELAY_MS);
    }
}
