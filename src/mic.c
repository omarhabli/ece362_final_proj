//since sel is grounded signal is on left of i2s frame
//PIO 0
//drive sck (gp20), ws(gp21)
//do (gp22) is input

/* FLOW
  - designated key(s) press will trigger an interrupt
  - interrupt handler will initialize/reset pio and dma
  - pio asm will stream data into pio fifo (can join)
  - dma transfers 1 word (24b) chunks of data into ram, tracks how much data has been sent
  - SPI STUFF TO MOVE DMA DATA INTO SD CARD
  - once 1s of data has been moved by dma, dma (OR PIO ASM) raises interrupt that disables the pio

*/

// I2S on GP20 (CLK), 21 (WS), 22 (DATA)

#include <stdio.h>
#include "pico/stdlib.h"
#include "mic.h"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "i2s_rx_master.pio.h"

static uint32_t mic_buf[48000];
static i2s_rx_master_t *g_mic_ctx = NULL;
volatile bool recording_done = false;


void mic_init(
    i2s_rx_master_t *ctx,
    PIO pio,
    uint sm,
    uint pin_sck,
    uint pin_ws,
    uint pin_sd,
    uint sample_rate,
    uint bits_per_slot
) {
    ctx->pio = pio;
    ctx->sm = sm;
    ctx->pin_sck = pin_sck;
    ctx->pin_ws = pin_ws;
    ctx->pin_sd = pin_sd;
    ctx->sample_rate = sample_rate;
    ctx->bits_per_slot = bits_per_slot;

    ctx->offset = pio_add_program(pio, &i2s_rx_master_program);

    pio_sm_config c = i2s_rx_master_program_get_default_config(ctx->offset);

    hard_assert(pin_ws == pin_sck + 1);
    sm_config_set_sideset_pins(&c, pin_sck);
    sm_config_set_in_pins(&c, pin_sd);

    // Autopush after 24 bits
    sm_config_set_in_shift(&c, false, true, 24);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

    sm_config_set_wrap(
        &c,
        ctx->offset + i2s_rx_master_wrap_target,
        ctx->offset + i2s_rx_master_wrap
    );

    // Your current PIO timing assumes 3 SM cycles per SCK bit
    const float sys_clk_hz = (float)clock_get_hz(clk_sys);
    const float bit_clk_hz = (float)sample_rate * (float)bits_per_slot * 2.0f;
    const float sm_clk_hz = bit_clk_hz * 3.0f;
    const float div = sys_clk_hz / sm_clk_hz;
    sm_config_set_clkdiv(&c, div);

    pio_gpio_init(pio, pin_sck);
    pio_gpio_init(pio, pin_ws);
    pio_gpio_init(pio, pin_sd);

    pio_sm_set_consecutive_pindirs(pio, sm, pin_sck, 2, true);
    pio_sm_set_consecutive_pindirs(pio, sm, pin_sd, 1, false);

    pio_sm_init(
        pio,
        sm,
        ctx->offset + i2s_rx_master_offset_entry_point,
        &c
    );

    // Known idle state
    pio_sm_set_pins_with_mask(
        pio,
        sm,
        0u,
        (1u << pin_sck) | (1u << pin_ws)
    );

    // Leave SM disabled here
    pio_sm_set_enabled(pio, sm, false);

    // Claim DMA once
    ctx->dma_chan = dma_claim_unused_channel(true);

    dma_channel_config dc = dma_channel_get_default_config(ctx->dma_chan);
    channel_config_set_transfer_data_size(&dc, DMA_SIZE_32);
    channel_config_set_read_increment(&dc, false);
    channel_config_set_write_increment(&dc, true);
    channel_config_set_dreq(&dc, pio_get_dreq(pio, sm, false));

    // Save this config by applying it once with no immediate start.
    dma_channel_configure(
        ctx->dma_chan,
        &dc,
        NULL,              // destination set later per capture
        &pio->rxf[sm],     // fixed source
        0,
        false
    );

    g_mic_ctx = ctx;

    dma_channel_set_irq0_enabled(ctx->dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, mic_done_dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void mic_start_capture(
    i2s_rx_master_t *ctx,
    uint32_t *buffer,
    size_t sample_count
) {
    // Stop SM first
    pio_sm_set_enabled(ctx->pio, ctx->sm, false);

    // Stop any old DMA transfer
    dma_channel_abort(ctx->dma_chan);

    // Clear stale RX data
    pio_sm_clear_fifos(ctx->pio, ctx->sm);

    // Reset SM internal state
    pio_sm_restart(ctx->pio, ctx->sm);

    // Re-arm DMA for this buffer
    dma_channel_set_read_addr(ctx->dma_chan, &ctx->pio->rxf[ctx->sm], false);
    dma_channel_set_write_addr(ctx->dma_chan, buffer, false);
    dma_channel_set_trans_count(ctx->dma_chan, sample_count, false);

    // Start DMA first so FIFO is drained immediately
    dma_start_channel_mask(1u << ctx->dma_chan);

    // Then start PIO
    pio_sm_set_enabled(ctx->pio, ctx->sm, true);
}

void mic_stop_capture(i2s_rx_master_t *ctx) {
    pio_sm_set_enabled(ctx->pio, ctx->sm, false);
}

void mic_done_dma_handler() {
    if (dma_hw->ints0 & (1u << g_mic_ctx->dma_chan)) {
        dma_hw->ints0 = 1u << g_mic_ctx->dma_chan;   // clear IRQ flag
        mic_stop_capture(g_mic_ctx);
        recording_done = true;
    }

    //ADD SPI to SD CARD FUNC HERE
}