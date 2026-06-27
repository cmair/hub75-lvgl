#include "pico/multicore.h"

#if defined(HUB75_SUPPORT)
#include "hub75.hpp"
#else
#define HUB75_SCREEN_WIDTH 96
#define HUB75_SCREEN_HEIGHT 64
#endif

#include "lvgl.h"

#include "bouncing_balls.hpp"
#include "fire_effect.hpp"
#include "image_animation.hpp"
#include "colour_check.hpp"

//--------------------------------------------------------------------------------
// Constants and Globals
//--------------------------------------------------------------------------------

#define BYTES_PER_PIXEL (LV_COLOR_FORMAT_GET_SIZE(LV_COLOR_FORMAT_RGB888)) ///< RGB888 color depth

/// @brief Enum for selecting animation demos
enum DemoIndex
{
    DEMO_AUTO = 0,
    DEMO_BOUNCE,
    DEMO_FIRE,
    DEMO_IMAGE,
    DEMO_COLOUR,
};

static critical_section_t crit_sec = {0};                              ///< Synchronization for safe time reading
static int frame_index = DEMO_BOUNCE;                                  ///< Current demo index
static uint8_t buf1[HUB75_SCREEN_WIDTH * HUB75_SCREEN_HEIGHT * BYTES_PER_PIXEL]; ///< Drawing buffer for LVGL

static lv_display_t *display1; ///< LVGL display handle

static bool load_anim = true; ///< Flag to trigger animation setup

BouncingBalls *bouncingBalls;
FireEffect *fireEffect;
ImageAnimation *imageAnimation;
ColourCheck *colourCheck;

struct repeating_timer timer;
bool timer_running = false;


//--------------------------------------------------------------------------------
// Utility Functions
//--------------------------------------------------------------------------------

/**
 * @brief Retrieve the number of milliseconds elapsed since system boot.
 *
 * This function returns a 32-bit unsigned integer representing the number of
 * milliseconds since the system was powered on or reset. It is safe to call
 * from within an LVGL tick callback and is designed to provide consistent time
 * values even when used in concurrent or interrupt-driven environments.
 *
 * The access to `get_absolute_time()` is wrapped in a critical section to
 * ensure atomicity and consistency on multicore or preemptive systems like the
 * RP2040. This prevents potential race conditions if `get_absolute_time()` is
 * not atomic.
 *
 * @return The time since boot in milliseconds.
 */
uint32_t get_milliseconds_since_boot()
{
    critical_section_enter_blocking(&crit_sec);
    uint32_t ms = to_ms_since_boot(get_absolute_time());
    critical_section_exit(&crit_sec);
    return ms;
}

/**
 * @brief Display flush callback for LVGL to update the Hub75 framebuffer.
 *
 * This function is called by LVGL when a part of the screen (or the entire screen)
 * needs to be flushed to the physical display. The pixel data is provided in
 * a linear buffer `px_map` which contains color data (e.g., in RGB888 format,
 * depending on LVGL configuration).
 *
 * For the Hub75 driver, we assume that the entire screen is updated each time
 * (full frame flush), and the buffer is passed to `update()` which converts
 * and writes it to the physical framebuffer or triggers a transfer.
 *
 * After the pixel data is processed, `lv_display_flush_ready()` must be called
 * to inform LVGL that the flush is complete, allowing it to reuse or update the
 * drawing buffer.
 *
 * @param display The LVGL display object.
 * @param area Area being updated (not used here).
 * @param px_map Pointer to pixel buffer.
 */
void flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    #if defined(HUB75_SUPPORT)
    update_bgr(px_map);              ///< Transfer buffer to display driver
    #endif
    lv_display_flush_ready(display); ///< Notify LVGL that flush is complete
}


/**
 * @brief Timer callback to cycle to the next demo.
 *
 * Called every 15 seconds to switch to the next animation mode.
 *
 * @param t Unused timer pointer.
 * @return true (always continue the timer).
 */
bool skip_to_next_demo(__unused struct repeating_timer *t)
{
    if (frame_index++ >= DEMO_COLOUR)
        frame_index = DEMO_BOUNCE;
    load_anim = true;
    return true;
}


/**
 * @brief Sets up the selected animation.
 *
 * This function initializes the animation scene based on the current demo index.
 *
 * @param index Current demo index.
 * @param bouncingBalls Bouncing ball animation instance.
 * @param fireEffect Fire effect instance.
 * @param imageAnimation Image animation instance.
 * @param colorCheck display colour squares
 * @param timer Reference to the demo-switching timer.
 */
void setup_demo(int index, BouncingBalls *bouncingBalls, FireEffect *fireEffect, ImageAnimation *imageAnimation, ColourCheck *colourCheck, struct repeating_timer &timer)
{
    switch (index)
    {
    case DEMO_BOUNCE:
        bouncingBalls->show();
        break;
    case DEMO_FIRE:
        fireEffect->show();
        break;
    case DEMO_IMAGE:
        cancel_repeating_timer(&timer); // prevent premature transition
        imageAnimation->show();
        imageAnimation->start();
        break;
    case DEMO_COLOUR:
        colourCheck->show();
        break;
    }
}

/**
 * @brief Updates the current animation each frame.
 *
 * Handles per-frame logic such as animation updates and polling for completion.
 *
 * @param index Current demo index.
 * @param bouncingBalls Bouncing ball animation instance.
 * @param fireEffect Fire effect instance.
 * @param imageAnimation Image animation instance.
 * @param colorCheck display colour squares
 * @param timer Reference to the demo-switching timer.
 */
void update_demo(int index, BouncingBalls *bouncingBalls, FireEffect *fireEffect, ImageAnimation *imageAnimation, ColourCheck *colourCheck, struct repeating_timer &timer)
{
    switch (index)
    {
    case DEMO_BOUNCE:
        bouncingBalls->bounce();
        break;
    case DEMO_FIRE:
        fireEffect->burn();
        break;
    case DEMO_IMAGE:
        if (imageAnimation->animation_done())
        {
            imageAnimation->animation_init();
            add_repeating_timer_ms(10000, skip_to_next_demo, NULL, &timer);
        }
        break;
    case DEMO_COLOUR:
//          skip_to_next_demo(NULL);
        colourCheck->colour_test();
        break;
    }
}


void lvgl_init()
{
    critical_section_init(&crit_sec);

    lv_init();

    // Set millisecond-based tick source for LVGL so that it can track time.
    lv_tick_set_cb(get_milliseconds_since_boot);

    // Create a display where screens and widgets can be added
    lv_display_t * display = lv_display_create(HUB75_SCREEN_WIDTH, HUB75_SCREEN_HEIGHT);
    if (!display)
    {
        panic("Failed to create LVGL display\n");
    }

    // Add rendering buffers to the screen.
//    lv_display_set_buffers(display, buf1, NULL, sizeof(buf1), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_buffers_with_stride(display, buf1, NULL, sizeof(buf1), HUB75_SCREEN_WIDTH * BYTES_PER_PIXEL, LV_DISPLAY_RENDER_MODE_FULL);

    // Add a callback that can flush the content from `buf` when it has been rendered
    lv_display_set_flush_cb(display, flush_cb);

    // Change the active screen's background color
//    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(0x000000), LV_PART_MAIN);

//    lv_display_set_rotation(display, LV_DISPLAY_ROTATION_180);

/*
    lv_obj_t * qr = lv_qrcode_create(lv_screen_active());
    lv_qrcode_set_dark_color(qr, { 0xFF, 0xFF, 0xFF });
    lv_qrcode_set_light_color(qr, { 0x00, 0x00, 0x00 });
    lv_obj_center(qr);
    lv_qrcode_set_size(qr, MIN(HUB75_SCREEN_WIDTH, HUB75_SCREEN_HEIGHT));
    // Set data
    const char * data = "Hello World, Hello World";
    lv_result_t res = lv_qrcode_update(qr, data, strlen(data));
*/


    bouncingBalls = new BouncingBalls(15, HUB75_SCREEN_WIDTH, HUB75_SCREEN_HEIGHT);
    fireEffect = new FireEffect(HUB75_SCREEN_WIDTH, HUB75_SCREEN_HEIGHT);
    imageAnimation = new ImageAnimation(64, 64);
    colourCheck = new ColourCheck(HUB75_SCREEN_WIDTH, HUB75_SCREEN_HEIGHT);


    add_repeating_timer_ms(15000, skip_to_next_demo, NULL, &timer);
    timer_running = true;
}


void lvgl_animate(int lvgl_demo)
{
    switch (lvgl_demo)
    {
        case -1:
            // Stop LVGL Demo (and timer)
            if (timer_running) {
                cancel_repeating_timer(&timer);
                timer_running = false;
            }
            return;
        case 0:
            // Start LVGL Demo (with autorotation)
            if (!timer_running) {
                add_repeating_timer_ms(15000, skip_to_next_demo, NULL, &timer);
                timer_running = true;
            }
            break;
        default:
            if (lvgl_demo != frame_index) {
                frame_index = lvgl_demo;
                load_anim = true;
            }
        break;
    }

    if (load_anim)
    {
        load_anim = false;
        setup_demo(frame_index, bouncingBalls, fireEffect, imageAnimation, colourCheck, timer);
    }
    update_demo(frame_index, bouncingBalls, fireEffect, imageAnimation, colourCheck, timer);

    lv_timer_handler();
}
