#include "protobuf.hpp"

#include <atomic>
#include <stdio.h>

#include "nanopb/pb_decode.h"
#include "nanopb/pb_common.h"
#include "panel.pb.h"

#include "fota.hpp"
#if defined(HUB75_SUPPORT)
#include "hub75.hpp"
#endif
#if defined(LVGL_SUPPORT)
#include "lvgl.hpp"
#endif


#if defined(HUB75_SUPPORT)
static std::atomic<bool> cmd_brightness_pending(false);
static std::atomic<bool> cmd_image_pending(false);

static std::atomic<bool> cmd_base_brightness_pending(false);
static std::atomic<uint32_t> pending_base_brightness(64);

static std::atomic<float> pending_brightness(0.0f);
static uint8_t pending_image[HUB75::TOTAL_PIXELS * 3];
#endif

#if defined(LVGL_SUPPORT)
static std::atomic<bool> cmd_demo_pending(false);
static std::atomic<int> demo_selection(0);
#endif

static std::atomic<size_t> fota_pending(0);
static uint8_t fota_data[1024];

void process_protobuf_message(const uint8_t *buf, size_t len)
{
    PanelCommand msg = PanelCommand_init_zero;
    pb_decode_ctx_t decode_ctx;
    pb_init_decode_ctx_for_buffer(&decode_ctx, buf, len);
    if (!pb_decode(&decode_ctx, PanelCommand_fields, &msg)) {
        printf("pb_decode failed: %s\n", PB_GET_ERROR(&decode_ctx));
        return;
    }

    switch (msg.which_command) {
        #if defined(HUB75_SUPPORT)
        case PanelCommand_set_brightness_tag:
            {
                float f = msg.command.set_brightness.brightness;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                pending_brightness.store(f);
                cmd_brightness_pending.store(true);
            }
            break;
        case PanelCommand_base_brightness_tag:
            {
                uint32_t base = msg.command.base_brightness.brightness;
                if (base > 255) {
                    base = 255;
                }
                pending_base_brightness.store(base);
                cmd_base_brightness_pending.store(true);
            }
            break;
        case PanelCommand_set_mode_tag:
            {
                int demo = msg.command.set_mode.mode - 2; // protobuf LVGL demos have an offset of two to DemoIndex enum in lvgl.cpp
                demo_selection.store(demo);
                cmd_demo_pending.store(true);
            }
            break;
        case PanelCommand_set_image_tag:
            {
                pb_bytes_array_t *pbimg = (pb_bytes_array_t *)&msg.command.set_image.image_data;
                size_t need = sizeof(pending_image);
                if ((pbimg != NULL) && (pbimg->size == need)) {
                    memcpy(pending_image, pbimg->bytes, need);
        //            printf("Received image of correct size: %u bytes\n", (unsigned)pbimg->size);
                    cmd_image_pending.store(true);
                } else {
                    printf("Received image with wrong size: %u (need %zu)\n", (unsigned)(pbimg ? pbimg->size : 0), need);
                }
            }
            break;
        #endif
        case PanelCommand_firmware_upload_tag:
            {
                pb_bytes_array_t *pbfota = (pb_bytes_array_t *)&msg.command.firmware_upload.firmware_data;
                memcpy(&fota_data, pbfota->bytes, pbfota->size);
                fota_pending.store(pbfota->size);
            }
            break;
        default:
            printf("Decoded command: Unknown (tag %d)\n", msg.which_command);
            return;
    }
}


void protobuf_firmware_block_confirmation(const uint8_t *buf, size_t len)
{

}


int protobuf_execute_pending_commands(void)
{
    #if defined(HUB75_SUPPORT)
    static int demo = 4;
    if (cmd_brightness_pending.exchange(false)) {
        float b = pending_brightness.load();
        setIntensity(b);
    }
    if (cmd_base_brightness_pending.exchange(false)) {
        uint32_t base = pending_base_brightness.load();
        setBasisBrightness(base);
    }
    if (cmd_image_pending.exchange(false)) {
        if (demo < 0) {
           update_bgr(pending_image);
        }
    }
    if (cmd_demo_pending.exchange(false)) {
        demo = demo_selection.load();
        printf("Selecting demo %d\n", demo);
        #if defined(LVGL_SUPPORT)
        lvgl_set_demo(demo);
        #endif
    }
    #endif
    if (size_t size = fota_pending.exchange(0)) {
        fota_process_data(fota_data, size);
    }
    return 0;
}
