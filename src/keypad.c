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
#include "queue.h"

// Global column variable
int col = -1;

// Global key state
static bool state[16]; // Are keys pressed/released

// Keymap for the keypad
const char keymap[17] = "DCBA#9630852*741";

// Defined here to avoid circular dependency issues with autotest
KeyEvents kev = { 
    .head = 0, 
    .tail = 0 
};


/********************************************************* */
// Implement the functions below.

void keypad_init_pins() {
    sio_hw->gpio_oe_clr = (1u << 2);
    sio_hw->gpio_clr = (1u << 2);
    pads_bank0_hw->io[2] = PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
    io_bank0_hw->io[2].ctrl = 5;
    
    sio_hw->gpio_oe_clr = (1u << 3);
    sio_hw->gpio_clr = (1u << 3);
    pads_bank0_hw->io[3] = PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
    io_bank0_hw->io[3].ctrl = 5;
    
    sio_hw->gpio_oe_clr = (1u << 4);
    sio_hw->gpio_clr = (1u << 4);
    pads_bank0_hw->io[4] = PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
    io_bank0_hw->io[4].ctrl = 5;
    
    sio_hw->gpio_oe_clr = (1u << 5);
    sio_hw->gpio_clr = (1u << 5);
    pads_bank0_hw->io[5] = PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_PDE_BITS;
    io_bank0_hw->io[5].ctrl = 5;
    
    sio_hw->gpio_oe_clr = (1u << 6);
    sio_hw->gpio_clr = (1u << 6);
    pads_bank0_hw->io[6] = PADS_BANK0_GPIO0_IE_BITS;
    io_bank0_hw->io[6].ctrl = 5;
    sio_hw->gpio_oe_set = (1u << 6);
    
    sio_hw->gpio_oe_clr = (1u << 7);
    sio_hw->gpio_clr = (1u << 7);
    pads_bank0_hw->io[7] = PADS_BANK0_GPIO0_IE_BITS;
    io_bank0_hw->io[7].ctrl = 5;
    sio_hw->gpio_oe_set = (1u << 7);
    
    sio_hw->gpio_oe_clr = (1u << 8);
    sio_hw->gpio_clr = (1u << 8);
    pads_bank0_hw->io[8] = PADS_BANK0_GPIO0_IE_BITS;
    io_bank0_hw->io[8].ctrl = 5;
    sio_hw->gpio_oe_set = (1u << 8);
    
    sio_hw->gpio_oe_clr = (1u << 9);
    sio_hw->gpio_clr = (1u << 9);
    pads_bank0_hw->io[9] = PADS_BANK0_GPIO0_IE_BITS;
    io_bank0_hw->io[9].ctrl = 5;
    sio_hw->gpio_oe_set = (1u << 9);
}


uint8_t keypad_read_rows() {
    uint8_t row_state = 0;
    row_state  = gpio_get(2);
    row_state += gpio_get(3) * 2;
    row_state += gpio_get(4) * 4;
    row_state += gpio_get(5) * 8;
    return row_state;
}

void keypad_drive_column() {
    timer0_hw->intr = 1;
    col = (col + 1) % 4;
    
    uint32_t mask = (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9);
    gpio_clr_mask(mask);
    gpio_set_mask(1u << (6 + col));
    
    timer0_hw->alarm[0] = timer0_hw->timerawl + 25000;
}

void keypad_isr() {
    timer0_hw->intr = (1u << 1);

    uint8_t row_pin = keypad_read_rows();
    uint16_t event;

    for (int row = 2; row <= 5; row++) {
        uint8_t index = col * 4 + (row - 2);
        event = 0;

        if ((row_pin >> (row - 2)) % 2) {
            if (!state[index]) {
                state[index] = 1;
                event |= (1u << 8);     
                event |= keymap[index];
                key_push(event);
            }
        } 
        else {
            if (state[index]) {
                state[index] = 0;
                event |= keymap[index];
                key_push(event);
            }
        }
    }
    timer0_hw->alarm[1] = timer0_hw->timerawl + 25000;
}


void keypad_init_timer() {
    irq_set_exclusive_handler(TIMER0_IRQ_0, keypad_drive_column);
    irq_set_exclusive_handler(TIMER0_IRQ_1, keypad_isr);

    irq_set_enabled(TIMER0_IRQ_0, true);
    irq_set_enabled(TIMER0_IRQ_1, true);

    timer0_hw->inte |= (1u << 0); 
    timer0_hw->inte |= (1u << 1); 

    timer0_hw->alarm[0] = timer0_hw->timerawl + 1000000;
    timer0_hw->alarm[1] = timer0_hw->timerawl + 1010000;
}