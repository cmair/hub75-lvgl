#include <cstring>
#include <cstdlib>
#include <atomic>

#include "network.hpp"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/cyw43_driver.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"

// Use nanopb for decoding protobuf messages
#include "nanopb/pb_decode.h"
#include "nanopb/pb_common.h"
#include "panel.pb.h"

#include "hub75.hpp"

static struct udp_pcb *udp_listener = nullptr;

static std::atomic<bool> brightness_pending(false);
static std::atomic<bool> image_pending(false);
static std::atomic<float> pending_brightness(0.0f);
static std::atomic<bool> demo_pending(false);
static std::atomic<int> demo_selection(0);
static uint8_t pending_image[TOTAL_PIXELS * 3];


static void process_panel_message(const uint8_t *buf, size_t len)
{
    PanelCommand msg = PanelCommand_init_zero;
    pb_decode_ctx_t decode_ctx;
    pb_init_decode_ctx_for_buffer(&decode_ctx, buf, len);
    if (!pb_decode(&decode_ctx, PanelCommand_fields, &msg)) {
        printf("pb_decode failed: %s\n", PB_GET_ERROR(&decode_ctx));
        return;
    }

    switch (msg.which_command) {
        case PanelCommand_set_brightness_tag:
            {
                float f = msg.command.set_brightness.brightness;
                if (f < 0.0f) f = 0.0f;
                if (f > 1.0f) f = 1.0f;
                pending_brightness.store(f);
                brightness_pending.store(true);
            }
            break;
        case PanelCommand_set_mode_tag:
            {
                int demo = msg.command.set_mode.mode - 2; // protobuf LVGL demos have an offset of two to DemoIndex enum in lvgl.cpp
                demo_selection.store(demo);
                demo_pending.store(true);
            }
            break;
        case PanelCommand_set_image_tag:
            {
                pb_bytes_array_t *pbimg = (pb_bytes_array_t *)&msg.command.set_image.image_data;
                size_t need = TOTAL_PIXELS * 3;
                if ((pbimg != NULL) && (pbimg->size == need)) {
                    memcpy(pending_image, pbimg->bytes, need);
        //            printf("Received image of correct size: %u bytes\n", (unsigned)pbimg->size);
                    image_pending.store(true);
                } else {
                    printf("Received image with wrong size: %u (need %zu)\n", (unsigned)(pbimg ? pbimg->size : 0), need);
                }
            }
            break;
        default:
            printf("Decoded command: Unknown (tag %d)\n", msg.which_command);
            return;
    }
}

static void udp_recv_cb(void *arg, struct udp_pcb *upcb, struct pbuf *p, const ip_addr_t *addr, u16_t port)
{
    (void)arg;
    (void)upcb;
    (void)addr;
    (void)port;

    if (!p) return;

    size_t total = p->tot_len;
    uint8_t *tmp = (uint8_t *)malloc(total);
    if (!tmp) {
        printf("udp_recv_cb: alloc failed for %u bytes\n", (unsigned)total);
        pbuf_free(p);
        return;
    }

    pbuf_copy_partial(p, tmp, total, 0);
    pbuf_free(p);

    process_panel_message(tmp, total);

    free(tmp);
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
        cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM);
        cyw43_arch_enable_sta_mode();

        // Register callback to be notified when DHCP assigns an IP
        netif_set_status_callback(&cyw43_state.netif[CYW43_ITF_STA], netif_status_callback);

        printf("Connecting to %s:%s\n", WIFI_SSID, WIFI_PWD);
        if (cyw43_arch_wifi_connect_async(WIFI_SSID, WIFI_PWD, CYW43_AUTH_WPA3_WPA2_AES_PSK)) {
            printf("WiFi failed to connect\n");
        } else {
            printf("WiFi is connecting...\n");
        }
    }
}

int network_service()
{
    static int demo = 1;
    if (brightness_pending.exchange(false)) {
        float b = pending_brightness.load();
        setIntensity(b);
    }
    if (image_pending.exchange(false)) {
        update_bgr(pending_image);
    }
    if (demo_pending.exchange(false)) {
        demo = demo_selection.load();
        printf("Selecting demo %d\n", demo);
    }
    return demo;
}
