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
#include "volume_adc.h"

#define ADC_PIN 45

void init_volume_adc() {
    adc_init();
    adc_gpio_init(ADC_PIN);
    adc_select_input(5); // ADC3 = GP45
}

uint16_t read_volume_raw() {

    // start conversion (LAB STYLE)
    adc_hw->cs |= (1 << 2);

    // wait for ready
    while (!(adc_hw->cs & (1 << 8)));

    uint16_t val = adc_hw->result & 0x0FFF;


    return val;
}

float read_volume_voltage() {
    uint16_t raw = read_volume_raw();
    float v = (raw * 3.3f) / 4095.0f;

    return v;
}

int read_volume_percent() {
    uint16_t raw = read_volume_raw();
    int p = (raw * 100) / 4095;


    return p;
}