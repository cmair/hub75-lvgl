#pragma once

#include <stdint.h>
#include <cstdlib>

void process_protobuf_message(const uint8_t *buf, size_t len);
int protobuf_execute_pending_commands(void);
