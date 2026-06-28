/**
 * Copyright (c) 2022 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string.h>
#include <stdlib.h>
#include <cstdio>

#include "pico/stdlib.h"
#include "pico/sha256.h"
#include "pico/bootrom.h"
#include "boot/picobin.h"
#include "boot/picoboot.h"
#include "boot/uf2.h"

#include "fota.hpp"

#define FLASH_SECTOR_ERASE_SIZE 4096u

typedef struct uf2_block uf2_block_t;

typedef struct FOTA_STATE_T_ {
    bool started;
    bool complete;
    __attribute__((aligned(4))) uint8_t buffer_sent[SHA256_RESULT_BYTES];
    __attribute__((aligned(4))) uint8_t buffer_recv[FOTA_BUF_SIZE];
    int recv_len;
    int num_blocks;
    int blocks_done;
    uint32_t family_id;
    uint32_t flash_update;
    int32_t write_offset;
    uint32_t write_size;
    uint32_t highest_erased_sector;
} FOTA_STATE_T;

static FOTA_STATE_T state;
static __attribute__((aligned(4))) uint8_t workarea[FLASH_SECTOR_ERASE_SIZE];

static fota_send_cb_t fota_send_cb = NULL;
static fota_complete_cb_t fota_complete_cb = NULL;


int fota_init(void) {
    memset(&state, 0, sizeof(state));
    state.recv_len = 0;
    state.num_blocks = 0;
    state.blocks_done = 0;
    state.highest_erased_sector = 0;
    state.started = false;
    state.complete = false;
    printf("FOTA initialized\n");
    return 0;
}


int fota_process_data(uint8_t *data, size_t len, struct proto_message_peer *src) {
    if (state.complete) {
        printf("FOTA already complete!");
        return -1;
    }
    size_t off = 0;
    while (off < len) {
        size_t to_copy = (len - off) < (size_t)(FOTA_BUF_SIZE - state.recv_len) ? (len - off) : (size_t)(FOTA_BUF_SIZE - state.recv_len);
        memcpy(state.buffer_recv + state.recv_len, data + off, to_copy);
        state.recv_len += to_copy;
        off += to_copy;

        if (state.recv_len == FOTA_BUF_SIZE) {
            // process buffer
            for (int i = 0; i < FOTA_BUF_SIZE / sizeof(uf2_block_t); i++) {
                uf2_block_t* block = (uf2_block_t*)(state.buffer_recv + i * sizeof(uf2_block_t));

                if (state.num_blocks == 0) {
                    state.num_blocks = block->num_blocks;
                    state.family_id = block->file_size;

                    resident_partition_t uf2_target_partition;
                    rom_flash_flush_cache();
                    int ret = rom_get_uf2_target_partition(workarea, sizeof(workarea), state.family_id, &uf2_target_partition);
                    if (ret < 0) {
                        printf("rom_get_uf2_target_partition error: %d", ret);
                        return -1;
                    }
                    printf("Code Target partition is %lx %lx\n", uf2_target_partition.permissions_and_location, uf2_target_partition.permissions_and_flags);

                    uint16_t first_sector_number = (uf2_target_partition.permissions_and_location & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
                    uint16_t last_sector_number = (uf2_target_partition.permissions_and_location & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >> PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
                    uint32_t code_start_addr = first_sector_number * 0x1000;
                    uint32_t code_end_addr = (last_sector_number + 1) * 0x1000;
                    uint32_t code_size = code_end_addr - code_start_addr;
                    printf("Start %lx, End %lx, Size %lx\n", code_start_addr, code_end_addr, code_size);

                    state.flash_update = code_start_addr + XIP_BASE;
                    state.write_offset = code_start_addr + XIP_BASE - block->target_addr;
                    state.write_size = code_size;

                    if ((uint32_t)(block->target_addr / FLASH_SECTOR_ERASE_SIZE) > state.highest_erased_sector) {
                        struct cflash_flags flags;
                        flags.flags =
                            (CFLASH_OP_VALUE_ERASE << CFLASH_OP_LSB) |
                            (CFLASH_SECLEVEL_VALUE_SECURE << CFLASH_SECLEVEL_LSB) |
                            (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB);
                        int ret = rom_flash_op(flags,
                            block->target_addr + state.write_offset,
                            FLASH_SECTOR_ERASE_SIZE, NULL);
                        printf("rom_flash_op returned %d\n", ret);
                        state.highest_erased_sector = block->target_addr / FLASH_SECTOR_ERASE_SIZE;
                    }

                }

                if (state.blocks_done != block->block_no) {
                    printf("block number mismatch - expected %d, got %d\n", state.blocks_done, block->block_no);
                    if (state.blocks_done > block->block_no) {
                        i++;
                        continue;
                    } else {
                        printf("block number mismatch - expected %d, got %d\n", state.blocks_done, block->block_no);
                        state.complete = true;
                        if (fota_complete_cb) {
                            printf("Fota complete cb -1(1)\n");
                            fota_complete_cb(-1);
                        } 
                        return -1;
                    }
                }
                if (state.family_id != block->file_size) {
                    printf("family id mismatch\n");
                    state.complete = true;
                    if (fota_complete_cb) {
                        printf("Fota complete cb -1(2)\n");
                        fota_complete_cb(-1);
                    } 
                    return -1;
                }

                struct cflash_flags flags;
                int8_t ret;
                flags.flags =
                    (CFLASH_OP_VALUE_PROGRAM << CFLASH_OP_LSB) |
                    (CFLASH_SECLEVEL_VALUE_SECURE << CFLASH_SECLEVEL_LSB) |
                    (CFLASH_ASPACE_VALUE_STORAGE << CFLASH_ASPACE_LSB);
                ret = rom_flash_op(flags,
                    block->target_addr + state.write_offset,
                    256, (uint8_t*)block->data);

                state.blocks_done++;
                if (state.blocks_done >= state.num_blocks) {
                    state.complete = true;
                    if (fota_complete_cb) {
                        printf("Fota complete cb 0\n");
                        fota_complete_cb(0);
                    }
                    // do not send sha for final block (client expects no reply on final)
                    state.recv_len = 0;
                    return 0;
                }
            }

            // Hash the received data and send via callback
            pico_sha256_state_t sha_state;
            int rc = pico_sha256_start_blocking(&sha_state, SHA256_BIG_ENDIAN, true);
            hard_assert(rc == PICO_OK);
            pico_sha256_update_blocking(&sha_state, (const uint8_t*)state.buffer_recv, sizeof(state.buffer_recv));
            pico_sha256_finish(&sha_state, (sha256_result_t*)state.buffer_sent);

            if (fota_send_cb) {
                int length = protobuf_firmware_block_confirmation(data, len, state.buffer_sent, SHA256_RESULT_BYTES);
                if (length > 0) {
                    fota_send_cb(data, length, src);
                } else {
                    printf("protobuf encoding failed!\n");
                    fota_init();
                    return -1;
                }
            }

            state.recv_len = 0;
        }
    }
    return 0;
}


void fota_set_callbacks(fota_complete_cb_t complete_cb, fota_send_cb_t send_cb){
    fota_complete_cb = complete_cb;
    fota_send_cb = send_cb;
}


 void fota_confirm(void) {
    boot_info_t boot_info = {};
    int ret = rom_get_boot_info(&boot_info);
    printf("Boot partition was %d\n", boot_info.partition);

    if (rom_get_last_boot_type() == BOOT_TYPE_FLASH_UPDATE) {
        printf("Someone updated into me\n");
        if (boot_info.reboot_params[0]) printf("Flash update base was %x\n", boot_info.reboot_params[0]);
        if (boot_info.tbyb_and_update_info) printf("Update info %x\n", boot_info.tbyb_and_update_info);
        ret = rom_explicit_buy(workarea, sizeof(workarea));
        if (ret) printf("Buy returned %d\n", ret);
        ret = rom_get_boot_info(&boot_info);
        if (boot_info.tbyb_and_update_info) printf("Update info now %x\n", boot_info.tbyb_and_update_info);
    }
}


bool fota_is_complete(void) {
    return state.complete;
}


void fota_reboot(void) {
    int ret = rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE, 1000, state.flash_update, 0);
    printf("Done - rebooting for a flash update boot %d\n", ret);
    sleep_ms(2000);
}
