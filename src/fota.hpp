#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOTA_BUF_SIZE 2048u

typedef int (*fota_send_cb_t)(const uint8_t *data, size_t len);
typedef void (*fota_complete_cb_t)(int status);

int fota_init(void);
int fota_process_data(const uint8_t *data, size_t len);

// Callbacks
void fota_set_callbacks(fota_complete_cb_t complete_cb, fota_send_cb_t send_cb);

void fota_confirm(void);
bool fota_is_complete(void);
void fota_reboot(void);
