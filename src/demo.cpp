#include <cstdio>

#include "pico/stdlib.h"
#include <pico/time.h>
#include <pico/multicore.h>

#include "hardware/clocks.h"

#include "protobuf.hpp"

#if defined(HUB75_SUPPORT)
#include "hub75.hpp"
#endif

#if defined(LVGL_SUPPORT)
#include "lvgl.hpp"
#endif

#if defined(UDP_VIDEO_SERVER_PORT)
#include "network.hpp"
#include "fota.hpp"
#endif

#if HUB75_MULTICORE == true
#include "pico/multicore.h"
#endif

#include "pico/bootrom.h"


/**
 * @brief Secondary core entry point.
 *
 * Initializes and starts the HUB75 driver on core 1.
 */
void core1_entry()
{
    // Enable flash lockout possibilities for FOTA on each core!
    flash_safe_execute_core_init();

#if defined(HUB75_SUPPORT)
    create_hub75_driver();
    start_hub75_driver();
#endif

    // Start network tasks on core 1 as well, so that they can run in parallel with the HUB75 driver
#if defined(UDP_VIDEO_SERVER_PORT)
    network_init();
#endif

    // KEEP CORE 1 ALIVE — without this, Core 1's NVIC is torn down and DMA_IRQ_1 stops firing
    //
    // Add your additional tasks for core1 here
    while (!fota_is_complete())
    {
        sleep_ms(10);
    }
    fota_reboot();
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

    // Enable flash lockout possibilities for FOTA on each core!
    flash_safe_execute_core_init();

    multicore_reset_core1();             // Reset core 1
    multicore_launch_core1(core1_entry); // Launch core 1 entry function - the Hub75 driver is doing its job there


#if defined(LVGL_SUPPORT)
    lvgl_init();
#endif

    // The Hub75 driver is constantly running on core 1 with a frequency usually much higher than 200Hz.
    // CPU load (on core 1) is low due to DMA and PIO usage.
    // The animated examples are updated at 100Hz.
    float hz = 100.0f;
    float ms = 1000.0f / hz;

#if defined(HUB75_SUPPORT)
    // set basis brightness of matrix panel
    setBasisBrightness(60);

    // set full brightness of panel
    float intensity = 0.25f;
    setIntensity(intensity);
#endif

    absolute_time_t time_start;
    while (true)
    {
        time_start = get_absolute_time();
        protobuf_execute_pending_commands();
#if defined(LVGL_SUPPORT)
        lvgl_animate();
#endif
        protobuf_execute_pending_commands();
        int64_t duration = absolute_time_diff_us(time_start, get_absolute_time());
        sleep_us((ms*1000) - duration); // hz updates per second - the HUB75 driver is running independently
    }
}
