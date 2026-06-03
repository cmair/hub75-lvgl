#include <cstdio>

#include "pico/stdlib.h"
#include <pico/time.h>


#include "hardware/clocks.h"

#include "hub75.hpp"

#include "lvgl.hpp"


#if HUB75_MULTICORE == true
#include "pico/multicore.h"
#endif



/**
 * @brief Secondary core entry point.
 *
 * Initializes and starts the HUB75 driver on core 1.
 */
void core1_entry()
{
    create_hub75_driver(DISPLAY_WIDTH, DISPLAY_HEIGHT, PANEL_TYPE, INVERTED_STB);
    start_hub75_driver();

    // KEEP CORE 1 ALIVE — without this, Core 1's NVIC is torn down and DMA_IRQ_1 stops firing
    //
    // Add your additional tasks for core1 here
    while (true)
    {
        tight_loop_contents();
    }
}


int main()
{
    // Set system clock to 234MHz - this is the highest supported frequency for my panels
    // Rendering is still light on CPU, so there is no need to go any higher and slow down PIO
    set_sys_clock_khz(234000, true);

    stdio_init_all(); // Initialize Pico SDK

    // Wait up to 2 seconds for USB serial to connect
    absolute_time_t timeout = make_timeout_time_ms(2000);
    while (!stdio_usb_connected() && !time_reached(timeout)) {
        sleep_ms(10);
    }
    printf("USB connected!\n");

#if HUB75_MULTICORE == true
    // Run hub75 driver on core1
    multicore_reset_core1();             // Reset core 1
    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there
#else
    // Run hub75 on core0 - the Hub75 driver is doing its job here
    create_hub75_driver(DISPLAY_WIDTH, DISPLAY_HEIGHT, PANEL_TYPE, INVERTED_STB);
    start_hub75_driver();
#endif

    lvgl_init();

    // The Hub75 driver is constantly running on core 1 with a frequency usually much higher than 200Hz.
    // CPU load (on core 1) is low due to DMA and PIO usage.
    // The animated examples are updated at 100Hz.
    float hz = 100.0f;
    float ms = 1000.0f / hz;

    // set basis brightness of matrix panel
    setBasisBrightness(60);

    // set full brightness of panel
    float intensity = 0.25f;
    setIntensity(intensity);

    while (true)
    {
        lvgl_animate();
        sleep_ms(ms); // hz updates per second - the HUB75 driver is running independently usually with far more than 200Hz (see README.md)
    }
}
