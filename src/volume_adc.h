#ifndef VOLUME_ADC_H
#define VOLUME_ADC_H

#include <stdint.h>

void init_volume_adc(void);
uint16_t read_volume_raw(void);
float read_volume_voltage(void);
int read_volume_percent(void);

#endif