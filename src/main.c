#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/timer.h"
#include "hardware/spi.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "hardware/adc.h"
#include "hardware/uart.h"

// State Variables
volatile bool record_pending = false;
volatile bool play_pending = false;

int main() {
    stdio_init_all();

    // Initialize all peripherals
    init_keypad();
    init_audio_pwm();
    init_sd_spi();
    init_tft_spi();
    init_mic_pio();
    init_volume_adc();

    while (1) {
        // Main Loop logic for asterisk (*) and zero (0) modes
        tight_loop_contents();
    }
    return 0;
}