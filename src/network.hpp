#pragma once

#include <cstddef>
#include <cstdint>

// Initialize UDP listener and protobuf handler (listens on port 5000)
// Each UDP datagram is expected to contain a single protobuf payload (no length prefix).
void network_init();

// Called regularly from main loop to apply pending updates.
int network_service();
