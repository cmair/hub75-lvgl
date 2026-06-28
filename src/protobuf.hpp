#pragma once

#include <stdint.h>
#include <cstdlib>

#include "lwip/udp.h"

enum proto_message_source {
    TCP,
    UDP,
    UART0,
    UART1,
};

struct proto_message_peer {
    enum proto_message_source source;
    union {
        uint32_t uart_id;
        struct {
            ip_addr_t addr;
            uint16_t port;
        };
    } peer;
};

void protobuf_init(void);
void process_protobuf_message(uint8_t *buf, size_t len, const struct proto_message_peer *proto_msg_source);
int protobuf_firmware_block_confirmation(uint8_t *dst, size_t dst_len, const uint8_t *payload, size_t len);
int protobuf_execute_pending_commands(void);
