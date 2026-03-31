#include <stdio.h>
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

#define PIN_SDI    11
#define PIN_CS     13
#define PIN_SCK    10
#define PIN_DC     14
#define PIN_nRESET 15
#define ADC_PIN 45   

void init_spi_lcd() {
    gpio_set_function(PIN_CS,     GPIO_FUNC_SIO);
    gpio_set_function(PIN_DC,     GPIO_FUNC_SIO);
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

int main() {
    stdio_init_all();

    init_spi_lcd();
    LCD_Setup();
    LCD_Clear(0x0000);

    init_volume_adc();   // 🔥 CHANGED HERE ONLY

    char buffer[50];

    while (1) {
        uint16_t adc_val = read_volume_raw();
        float voltage    = (adc_val * 3.3f) / 4095.0f;
        int volume       = (adc_val * 100) / 4095;

        // ===== TEXT — black bg baked in, no separate clear needed =====
        sprintf(buffer, "ADC: %d   ", adc_val);   // trailing spaces overwrite old digits
        LCD_DrawString(10, 5,  0xFFFF, 0x0000, buffer, 16, 0);

        sprintf(buffer, "V: %.2f   ", voltage);
        LCD_DrawString(10, 20, 0x07E0, 0x0000, buffer, 16, 0);

        sprintf(buffer, "VOL: %d%%  ", volume);
        LCD_DrawString(10, 35, 0xFFE0, 0x0000, buffer, 16, 0);

        // ===== BAR — draw full background first, then fill on top =====
        // This is one continuous rectangle operation, not clear+draw
        int bar_width = (volume * 200) / 100;
        LCD_DrawFillRectangle(10,            50, 10 + bar_width, 65, 0x07E0); // filled
        LCD_DrawFillRectangle(10 + bar_width, 50, 210,           65, 0x4208); // unfilled remainder

        sleep_ms(50);
    }
}

////OK LET ME MAKE MYSELF CLEAR. I AM DOING A MICROPROCESSOR EMBEDDED SYSTEM PROJECT WHERE I WILL HAVE 6 PERIPHERALS. ONE MIC SPEAKING TO AN SD CARD IN PIO TO IMPLEMENT I2S. ONE KEYBOARD TALKING TO THE MICROCONTROLLER IN GPIO. ONE POT TALKING TO THE MICROCONTROLLER FOR ADC (VOLUME KNOB). A TFT DISPLAY (SPI). THE SD CARD TLAKING TO THE MICROCONTROLLER GIVING AUDIO FILES RECORD FROM THE MICROCONTROLLER ALSO IN SPI. DO YOU UNDERSTAND? THE MICROCONTROLLER WE ARE USING IS AN RP2350. WHEN YOU CLICK THE BUTTON 0, YOU ARE IN THE PLAYING STATE. IF YOU CLICK THE BUTTON ASTERISK YOU ARE IN THE RECORDING STATE. THE AUDIO'S WILL BE ONE SECOND LONG. BASICALLY ONCE YOU CLICK ASTERISK, YOU CAN CLICK ANY OTHER BUTTON ON THE KEYPAD OTHER THAN 0 AND RECORED A SOUND FOR IT WITHIN ONE SECOND. I WANT THE TFT DISPLAY TO DISPLAY THE VOLUME AND THE RECORDING STATE (MAYBE A SMALL COOL LITTLE TIMER TO SAY HOW MUCH YOU HAVE LEFT IN RECORDING THAT DECAYS, 1 SECOND). HERE ARE THE PINS BEING USED: Component Pins Peripheral PWM Audio Out GP0 Vcc Gnd PWM0 A Keypad (4x4) GP2, 3, 4, 5 (Rows) / GP6, 7, 8, 9 (Cols) SIO (GPIO) TFT Display GP10 (SCK), 11 (TX), 12 (RX), 13 (CSn) SPI1 TFT Control GP14 (DC), 15 (RST) Vcc Gnd SIO (GPIO) SD Card GP16 (RX), 17 (CSn), 18 (SCK), 19 (TX) Vcc Gnd SPI0 Microphone GP20 (CLK), 21 (WS), 22 (DATA) Vcc Gnd PIO0 Volume ADC GP45 Vcc Gnd for Pot ADC3 CAN WE START WORKING TOGETHER? DONT MAKE ANY SOURCE FILES YET, JUST START THINKING ABOUT THE GEENRAL MAIN.C FILE AND SUCH WHILE I SET UP TFT AND THEN WHEN I NEED YOUR HELP WE CAN TALK. REPEAT TO ME WHAT YOU UNDERSTOOD