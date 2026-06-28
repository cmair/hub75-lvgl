#include <cstring>
#include <cstdlib>
#include <atomic>

#include "network.hpp"
#include "fota.hpp"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"

#include "protobuf.hpp"

#if defined(HUB75_SUPPORT)
#include "hub75.hpp"
#define BUFSIZE (10 + HUB75::TOTAL_PIXELS * 3)
#else
#define BUFSIZE 2048u
#endif

static struct udp_pcb *udp_listener = nullptr;
static uint8_t receive_buffer[BUFSIZE];
static struct proto_message_peer source;

static void udp_recv_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)upcb;
    (void)addr;
    (void)port;

    if (!p) {
        return;
    }

    size_t total = p->tot_len;
    // reserve a static buffer with enough space for the image data and some extra bytes for protobuf overhead
    if (!receive_buffer) {
        printf("udp_recv_cb: alloc failed for %u bytes\n", (unsigned)total);
        pbuf_free(p);
        return;
    }

    pbuf_copy_partial(p, receive_buffer, total, 0);
    pbuf_free(p);

    printf("Received packet of size %d\n", total);

    source.source = UDP;
    source.peer.addr = *addr;
    source.peer.port = port;

    process_protobuf_message(receive_buffer, total, &source);
}


static int udp_send_message(uint8_t *data, size_t size, const struct proto_message_peer *dst)
{
    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, size, PBUF_RAM);
    if (p != NULL) {
        memcpy(p->payload, data, size);
        int err = udp_sendto(udp_listener, p, &(dst->peer.addr), UDP_VIDEO_SERVER_PORT);
        if (err < 0){
            printf("UDP send returned err %d\n", err);
        }
        pbuf_free(p);
    } else {
        printf("Could not allocate UDP send buffer of size %d\n", size);
    }
    return 0;
}


static void network_fota_complete_cb_impl(int status)
{
    if (status == 0) {
        printf("FOTA completed. Rebooting...");
        fota_reboot();
    } else {
        printf("FOTA reported an error. Aborting.");
        fota_init();
    }
}

void start_udp_server()
{
    cyw43_arch_lwip_check();
    udp_listener = udp_new();
    if (!udp_listener) {
        printf("Failed to create UDP server\n");
        return;
    }

    err_t err = udp_bind(udp_listener, IP_ADDR_ANY, UDP_VIDEO_SERVER_PORT);
    if (err != ERR_OK) {
        printf("UDP bind failed: %d\n", err);
        udp_remove(udp_listener);
        udp_listener = nullptr;
        return;
    }

    // Set receiver callback
    udp_recv(udp_listener, udp_recv_cb, nullptr);
    printf("UDP server started on port %d\n", UDP_VIDEO_SERVER_PORT);
    fota_set_callbacks(network_fota_complete_cb_impl, udp_send_message);
}


/**
 * @brief Callback invoked when network interface status changes (e.g., when DHCP acquires an IP).
 */
static void netif_status_callback(struct netif *netif)
{
    cyw43_arch_lwip_check();
    printf("Network interface status changed: %s\n", netif_is_up(netif) ? "up" : "down");
    if (netif_is_up(netif)) {
        const ip4_addr_t *ip = netif_ip4_addr(netif);
        printf("IP address acquired: %u.%u.%u.%u\n",
            ip4_addr1(ip), ip4_addr2(ip), ip4_addr3(ip), ip4_addr4(ip));
        fota_confirm();
        // Start network services once we have an IP
        start_udp_server();
    }
}


void network_init()
{
    cyw43_set_pio_clock_divisor(3, 0);
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SWITZERLAND)) {
        printf("WiFi failed to initialise\n");
    } else {
        printf("WiFi initialized!\n");
        cyw43_wifi_pm(&cyw43_state, CYW43_NONE_PM);
        cyw43_arch_enable_sta_mode();

        // Register callback to be notified when DHCP assigns an IP
        netif_set_status_callback(&cyw43_state.netif[CYW43_ITF_STA], netif_status_callback);

        printf("Connecting to %s:%s\n", WIFI_SSID, WIFI_PASSWORD);
        if (cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PASSWORD, CYW43_AUTH_WPA3_WPA2_AES_PSK)) {
            printf("WiFi failed to connect\n");
        } else {
            printf("WiFi is connecting...\n");
        }
    }
}
