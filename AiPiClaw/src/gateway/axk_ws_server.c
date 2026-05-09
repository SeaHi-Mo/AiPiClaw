/**
 * @file axk_ws_server.c
 * @brief WebSocket service器module - 基于 lwIP netconn 最简实现
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note support  WebSocket 握手、文本帧recv&send
 */

#include "axk_ws_server.h"
#include "axk_platform.h"
#include "axk_message_bus.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"
#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"

#include "web_ui.h"

#define WS_MAGIC_STRING     "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_MAX_PAYLOAD      2048
#define WS_LISTEN_BACKLOG   4
#define WS_CLIENT_STACK     4096
#define WS_CLIENT_PRIO      (configMAX_PRIORITIES - 3)

#include "semphr.h"
#include "mimi_config.h"

typedef struct {
    struct netconn *conn;
    bool handshaked;
} ws_client_t;

static const char *TAG = "ws_srv";
static bool s_ws_running = false;
static TaskHandle_t s_ws_task = NULL;
static struct netconn *s_ws_listener = NULL;
static uint16_t s_ws_port = 0;
static ws_client_t s_clients[MIMI_WS_MAX_CLIENTS];
static SemaphoreHandle_t s_ws_mutex = NULL;

/**
 * @brief compute  Sec-WebSocket-Accept
 */
static int ws_compute_accept(const char *key, char *out_accept, size_t out_len)
{
    char concat[128];
    unsigned char sha1_out[20];
    size_t key_len = strlen(key);
    size_t magic_len = strlen(WS_MAGIC_STRING);
    size_t olen = 0;

    if (key_len + magic_len >= sizeof(concat)) {
        return -1;
    }
    memcpy(concat, key, key_len);
    memcpy(concat + key_len, WS_MAGIC_STRING, magic_len);
    concat[key_len + magic_len] = '\0';

    mbedtls_sha1((const unsigned char *)concat, key_len + magic_len, sha1_out);

    if (mbedtls_base64_encode((unsigned char *)out_accept, out_len, &olen,
                               sha1_out, sizeof(sha1_out)) != 0) {
        return -1;
    }
    out_accept[olen] = '\0';
    return 0;
}

/**
 * @brief process WebSocket 握手
 */
static bool ws_do_handshake(struct netconn *client)
{
    char rx_buf[512];
    char ws_key[64] = "";
    char ws_accept[64];
    err_t err;

    /* recv HTTP request头 */
    struct netbuf *buf = NULL;
    err = netconn_recv(client, &buf);
    if (err != ERR_OK || !buf) {
        return false;
    }
    char *data = NULL;
    u16_t len = 0;
    netbuf_data(buf, (void **)&data, &len);
    if (len >= sizeof(rx_buf)) {
        len = sizeof(rx_buf) - 1;
    }
    memcpy(rx_buf, data, len);
    rx_buf[len] = '\0';
    netbuf_delete(buf);

    /* Browser HTTP GET (no WebSocket upgrade) -> serve Web UI.
     * Must NOT match WebSocket upgrade requests (they also start with "GET / HTTP") */
    if ((strncmp(rx_buf, "GET / ", 6) == 0 || strncmp(rx_buf, "GET / HTTP", 10) == 0)
        && strstr(rx_buf, "Upgrade: websocket") == NULL) {
        char resp_hdr[256];
        int hdr_len = snprintf(resp_hdr, sizeof(resp_hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: %u\r\n"
            "Connection: close\r\n"
            "\r\n", (unsigned int)WEB_UI_HTML_LEN);
        netconn_write(client, resp_hdr, hdr_len, NETCONN_COPY);
        netconn_write(client, WEB_UI_HTML, WEB_UI_HTML_LEN, NETCONN_COPY);
        return false;  /* close after serving page */
    }

    /* parse  Sec-WebSocket-Key */
    const char *key_hdr = "Sec-WebSocket-Key: ";
    char *p = strstr(rx_buf, key_hdr);
    if (!p) {
        AXK_LOG_WARN("[%s] 缺少 Sec-WebSocket-Key\r\n", TAG);
        return false;
    }
    p += strlen(key_hdr);
    char *end = strstr(p, "\r\n");
    if (!end) {
        return false;
    }
    size_t key_len = end - p;
    if (key_len >= sizeof(ws_key)) {
        key_len = sizeof(ws_key) - 1;
    }
    memcpy(ws_key, p, key_len);
    ws_key[key_len] = '\0';

    /* compute  accept */
    if (ws_compute_accept(ws_key, ws_accept, sizeof(ws_accept)) != 0) {
        return false;
    }

    /* send 101 response */
    char resp[256];
    int resp_len = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n", ws_accept);

    err = netconn_write(client, resp, resp_len, NETCONN_COPY);
    if (err != ERR_OK) {
        return false;
    }
    return true;
}

/**
 * @brief send WebSocket 文本帧
 */
static int ws_send_text(struct netconn *client, const char *text)
{
    size_t len = strlen(text);
    uint8_t hdr[4];
    size_t hdr_len = 0;

    hdr[0] = 0x81; /* FIN=1, opcode=text */
    if (len < 126) {
        hdr[1] = (uint8_t)len;
        hdr_len = 2;
    } else if (len < 65536) {
        hdr[1] = 126;
        hdr[2] = (len >> 8) & 0xFF;
        hdr[3] = len & 0xFF;
        hdr_len = 4;
    } else {
        /* not support 超长帧 */
        return -1;
    }

    err_t err = netconn_write(client, hdr, hdr_len, NETCONN_COPY);
    if (err != ERR_OK) {
        return -1;
    }
    err = netconn_write(client, text, len, NETCONN_COPY);
    if (err != ERR_OK) {
        return -1;
    }
    return 0;
}

/**
 * @brief will 客户端register to 全局list 
 */
static void ws_client_register(struct netconn *client)
{
    int i;
    if (!s_ws_mutex) {
        return;
    }
    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].conn == NULL) {
            s_clients[i].conn = client;
            s_clients[i].handshaked = true;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

/**
 * @brief from 全局list unreg 客户端
 */
static void ws_client_unregister(struct netconn *client)
{
    int i;
    if (!s_ws_mutex) {
        return;
    }
    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].conn == client) {
            s_clients[i].conn = NULL;
            s_clients[i].handshaked = false;
            break;
        }
    }
    xSemaphoreGive(s_ws_mutex);
}

/**
 * @brief process客户端connect
 */
static void ws_handle_client(struct netconn *client)
{
    if (!ws_do_handshake(client)) {
        netconn_close(client);
        netconn_delete(client);
        return;
    }
    AXK_LOG_INFO("[%s] WebSocket 握手OK\r\n", TAG);
    ws_client_register(client);

    while (s_ws_running) {
        struct netbuf *buf = NULL;
        err_t err = netconn_recv(client, &buf);
        if (err != ERR_OK || !buf) {
            break;
        }

        uint8_t *data = NULL;
        u16_t len = 0;
        netbuf_data(buf, (void **)&data, &len);
        if (len < 2) {
            netbuf_delete(buf);
            continue;
        }

        uint8_t opcode = data[0] & 0x0F;
        bool masked = (data[1] & 0x80) != 0;
        uint64_t payload_len = data[1] & 0x7F;
        size_t offset = 2;

        if (payload_len == 126) {
            if (len < 4) {
                netbuf_delete(buf);
                continue;
            }
            payload_len = ((uint64_t)data[2] << 8) | data[3];
            offset = 4;
        } else if (payload_len == 127) {
            /* not support 超长帧 */
            netbuf_delete(buf);
            continue;
        }

        uint8_t mask_key[4] = {0};
        if (masked) {
            if (len < offset + 4) {
                netbuf_delete(buf);
                continue;
            }
            memcpy(mask_key, data + offset, 4);
            offset += 4;
        }

        if (len < offset + payload_len) {
            netbuf_delete(buf);
            continue;
        }

        /* decode  payload */
        if (payload_len > 0 && payload_len <= WS_MAX_PAYLOAD) {
            char *msg = (char *)malloc(payload_len + 1);
            if (msg) {
                memcpy(msg, data + offset, (size_t)payload_len);
                msg[payload_len] = '\0';
                if (masked) {
                    for (size_t i = 0; i < payload_len; i++) {
                        msg[i] ^= mask_key[i % 4];
                    }
                }

                if (opcode == 0x01 || opcode == 0x00) {
                    /* 文本帧 or 连续帧 */
                    AXK_LOG_INFO("[%s] 收 to  WS msg: %s\r\n", TAG, msg);
                    mimi_msg_t m = {0};
                    strncpy(m.channel, MIMI_CHAN_WEBSOCKET, sizeof(m.channel) - 1);
                    m.content = msg;
                    m.priority = MIMI_PRIO_NORMAL;  /**< WebSocket usermsg */
                    axk_message_bus_push_inbound(&m);
                } else if (opcode == 0x08) {
                    /* close 帧 */
                    free(msg);
                    netbuf_delete(buf);
                    break;
                } else if (opcode == 0x09) {
                    /* ping 帧 - 回应 pong */
                    uint8_t pong_hdr[2] = {0x8A, 0x00};
                    netconn_write(client, pong_hdr, 2, NETCONN_COPY);
                }
                free(msg);
            }
        }
        netbuf_delete(buf);
    }

    ws_client_unregister(client);
    netconn_close(client);
    netconn_delete(client);
    AXK_LOG_INFO("[%s] 客户端disconnect\r\n", TAG);
}

/**
 * @brief WebSocket service器task
 */
static void ws_server_task(void *param)
{
    (void)param;
    struct netconn *listener = netconn_new(NETCONN_TCP);
    if (!listener) {
        AXK_LOG_ERROR("[%s] create netconn FAIL\r\n", TAG);
        s_ws_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    netconn_bind(listener, IP_ADDR_ANY, s_ws_port);
    netconn_listen_with_backlog(listener, WS_LISTEN_BACKLOG);
    s_ws_listener = listener;
    AXK_LOG_INFO("[%s] WebSocket service器listen port  %d\r\n", TAG, s_ws_port);

    while (s_ws_running) {
        struct netconn *client = NULL;
        err_t err = netconn_accept(listener, &client);
        if (err != ERR_OK || !client) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        AXK_LOG_INFO("[%s] 新客户端connect\r\n", TAG);
        ws_handle_client(client);
    }

    netconn_close(listener);
    netconn_delete(listener);
    s_ws_listener = NULL;
    s_ws_task = NULL;
    vTaskDelete(NULL);
}

int axk_ws_server_init(void)
{
    s_ws_running = false;
    s_ws_task = NULL;
    s_ws_listener = NULL;
    memset(s_clients, 0, sizeof(s_clients));
    if (s_ws_mutex) {
        vSemaphoreDelete(s_ws_mutex);
    }
    s_ws_mutex = xSemaphoreCreateMutex();
    AXK_LOG_INFO("[%s] WebSocket service器init\r\n", TAG);
    return 0;
}

int axk_ws_server_start(uint16_t port)
{
    if (s_ws_running) {
        return 0;
    }
    s_ws_port = port ? port : MIMI_WS_PORT;
    s_ws_running = true;

    if (xTaskCreate(ws_server_task, "ws_srv", 6144, NULL,
                    configMAX_PRIORITIES - 5, &s_ws_task) != pdPASS) {
        s_ws_running = false;
        return -1;
    }
    return 0;
}

/**
 * @brief  to current connect WebSocket 客户端send文本msg
 */
int axk_ws_server_send(const char *text)
{
    int i;
    int sent = 0;

    if (!text || !s_ws_mutex) {
        return -1;
    }

    if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }

    for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
        if (s_clients[i].conn != NULL && s_clients[i].handshaked) {
            if (ws_send_text(s_clients[i].conn, text) == 0) {
                sent++;
            }
        }
    }

    xSemaphoreGive(s_ws_mutex);
    return (sent > 0) ? 0 : -1;
}
