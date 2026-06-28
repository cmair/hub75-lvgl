#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "protobuf.hpp"

#define FOTA_BUF_SIZE 2048u

struct fota_message {
    uint8_t data [FOTA_BUF_SIZE];
    size_t size;
    struct proto_message_peer source;
};


typedef int (*fota_send_cb_t)(uint8_t *data, size_t len, const struct proto_message_peer *dst);
typedef void (*fota_complete_cb_t)(int status);

int fota_init(void);
int fota_process_data(uint8_t *data, size_t len, struct proto_message_peer *src);

// Callbacks
void fota_set_callbacks(fota_complete_cb_t complete_cb, fota_send_cb_t send_cb);

void fota_confirm(void);
bool fota_is_complete(void);
void fota_reboot(void);
