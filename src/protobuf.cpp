#include "protobuf.hpp"

#include <atomic>
#include <stdio.h>

#include <pico/sync.h>

#include "nanopb/pb_decode.h"
#include "nanopb/pb_encode.h"
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

static std::atomic<bool> fota_pending(false);
static struct fota_message fota_message;

static semaphore_t sem_panelCommand;
static PanelCommand msg = PanelCommand_init_zero;

void process_protobuf_message(uint8_t *buf, size_t len, const struct proto_message_peer *proto_msg_source)
{
    if (!sem_acquire_timeout_ms(&sem_panelCommand, 10)) {
        printf("Error aquiring semaphore");
        return;
    }
    msg = PanelCommand_init_zero;
    pb_decode_ctx_t decode_ctx;
    pb_init_decode_ctx_for_buffer(&decode_ctx, buf, len);
    if (!pb_decode(&decode_ctx, PanelCommand_fields, &msg)) {
        printf("pb_decode failed: %s\n", PB_GET_ERROR(&decode_ctx));
        sem_release(&sem_panelCommand);
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
                //fota_pending.store(false);
                pb_bytes_array_t *pbfota = (pb_bytes_array_t *)&msg.command.firmware_upload.firmware_data;
                memcpy(&fota_message.data, pbfota->bytes, pbfota->size);
                fota_message.size = pbfota->size;
                memcpy(&fota_message.source, proto_msg_source, sizeof(*proto_msg_source));
                fota_pending.store(true);
            }
            break;
        default:
            printf("Decoded command: Unknown (tag %d)\n", msg.which_command);
            return;
    }
    sem_release(&sem_panelCommand);
}


int protobuf_firmware_block_confirmation(uint8_t *dst, size_t dst_len, const uint8_t *payload, size_t len)
{
    static pb_encode_ctx_t encode_ctx;

    if (!sem_acquire_timeout_ms(&sem_panelCommand, 10)) {
        printf("Error aquiring semaphore 2");
        return 0;
    }
    msg = PanelCommand_init_zero;
    pb_init_encode_ctx_for_buffer(&encode_ctx, dst, 40);

    msg.which_command = PanelCommand_firmware_block_confirmation_tag;
    msg.command.firmware_block_confirmation.block_hash.size = len;
    memcpy(&msg.command.firmware_block_confirmation.block_hash.bytes, payload, len);
    int status = pb_encode(&encode_ctx, PanelCommand_fields, &msg);
    sem_release(&sem_panelCommand);

    if (!status) {
        printf("Encodig failed (%d): %s\n", status, PB_GET_ERROR(&encode_ctx));
        return -1;
    }
    return encode_ctx.bytes_written;
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
    if (fota_pending.exchange(false)) {
        fota_process_data(fota_message.data, fota_message.size, &fota_message.source);
    }
    return 0;
}

void protobuf_init(void)
{
    sem_init(&sem_panelCommand, 1, 1);
    printf("Semaphore initialized");
}
