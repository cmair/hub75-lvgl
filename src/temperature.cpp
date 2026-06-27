#include "temperature.hpp"

#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */
float temperature_read_mcu(void) {
    
    adc_set_temp_sensor_enabled(true);
    adc_select_input(4);

    /* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
    const float conversionFactor = 3.3f / (1 << 12);

    float adc = (float)adc_read() * conversionFactor;
    float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

    adc_set_temp_sensor_enabled(false);
    printf("Onboard temperature = %.02f°C\n", tempC);
    return tempC;
}

void temperature_init(void)
{
    adc_init();
}
