#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "ff.h"
#include "diskio.h"
#include <stdio.h>
#include <string.h>

/*******************************************************************/

#define SD_MISO -1
#define SD_CS -1
#define SD_SCK -1
#define SD_MOSI -1

/*******************************************************************/

void init_spi_sdcard() {
    // fill in.
}

void disable_sdcard() {
    // fill in.
}

void enable_sdcard() {
    // fill in.
}

void sdcard_io_high_speed() {
    // fill in.
}

void init_sdcard_io() {
    // fill in.
}

/*******************************************************************/

void init_uart();
void init_uart_irq();
void date(int argc, char *argv[]);
void command_shell();

int main() {
    // Initialize the standard input/output library
    init_uart();
    init_uart_irq();
    
    init_sdcard_io();
    
    // SD card functions will initialize everything.
    command_shell();

    for(;;);
}