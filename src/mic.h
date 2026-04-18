#ifndef MIC_H
#define MIC_H

#include "pico/stdlib.h"
#include "hardware/pio.h"

typedef struct {
    PIO pio;
    uint sm;
    uint offset;
    uint pin_sck;
    uint pin_ws;
    uint pin_sd;
    uint sample_rate;
    uint bits_per_slot;
    int dma_chan;
} i2s_rx_master_t;

void mic_init(
    i2s_rx_master_t *ctx,
    PIO pio,
    uint sm,
    uint pin_sck,
    uint pin_ws,
    uint pin_sd,
    uint sample_rate, //48000
    uint bits_per_slot //32
);

void mic_start_capture(
    i2s_rx_master_t *ctx,
    uint32_t *buffer,
    size_t sample_count //should be 48000
);

void mic_stop_capture(i2s_rx_master_t *ctx);
void mic_done_dma_handler(void);

#endif