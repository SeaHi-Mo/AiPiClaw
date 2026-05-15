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
#define WS_MAX_PAYLOAD      4096  /* 原2048，LLM长回复可达2800+ */
#define WS_LISTEN_BACKLOG   4
#define WS_CLIENT_STACK     8192
#define WS_CLIENT_PRIO      (configMAX_PRIORITIES - 3)

#include "semphr.h"
#include "mimi_config.h"

typedef struct {
    struct netconn *conn;
    bool handshaked; /**< WebSocket握手是否已完成 */
    SemaphoreHandle_t write_mutex; /**< 串行化对该客户端的 netconn_write */
} ws_client_t;

static const char *TAG = "ws_srv";
static bool s_ws_running = false;
static TaskHandle_t s_ws_task = NULL;
static struct netconn *s_ws_listener = NULL;
static uint16_t s_ws_port = 0;
static ws_client_t s_clients[MIMI_WS_MAX_CLIENTS];
static SemaphoreHandle_t s_ws_mutex = NULL;

/* pending 消息队列：LLM回复到达时无WS客户端时缓存 */
#define WS_PENDING_MAX  8
static char *s_pending[WS_PENDING_MAX];
static int s_pending_head = 0;
static int s_pending_count = 0;
static SemaphoreHandle_t s_pending_mutex = NULL;

/**
 * @brief 计算WebSocket握手所需的Sec-WebSocket-Accept值
 *
 * @param key 客户端发送的Sec-WebSocket-Key
 * @param out_accept 输出缓冲区，存放计算后的Accept值
 * @param out_len 输出缓冲区大小
 * @return 0成功，-1失败
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
 * @brief 处理WebSocket握手，解析HTTP升级请求并发送101响应
 *
 * @param client 客户端netconn连接
 * @return true握手成功，false握手失败或为普通HTTP请求
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
            "\r\n", (unsigned int)strlen(WEB_UI_HTML));
        netconn_write(client, resp_hdr, hdr_len, NETCONN_COPY);
        netconn_write(client, WEB_UI_HTML, strlen(WEB_UI_HTML), NETCONN_COPY);
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
        AXK_LOG_WARN("[%s] Sec-WebSocket-Key truncated (%u -> %u)\r\n", TAG,
                     (unsigned int)key_len, (unsigned int)(sizeof(ws_key) - 1));
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
 * @brief 发送WebSocket文本帧到指定客户端（写入时持write_mutex避免多任务并发损坏帧）
 *
 * @param client 目标客户端netconn连接
 * @param text 待发送的文本字符串
 * @return 0成功，-1失败
 */
#define ERR_VAL  (-6)  /* lwIP err.h */

static int ws_send_text(struct netconn *client, const char *text)
{
    SemaphoreHandle_t write_mutex = NULL;
    int found_idx = -1;
    {
        int i;
        for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
            if (s_clients[i].conn == client && s_clients[i].write_mutex) {
                write_mutex = s_clients[i].write_mutex;
                found_idx = i;
                break;
            }
        }
    }
    printf("[WS] send_text enter client=%p write_mutex=%p idx=%d\r\n",
           (void *)client, (void *)write_mutex, found_idx);
    if (write_mutex) {
        xSemaphoreTake(write_mutex, portMAX_DELAY);
        printf("[WS] send_text took write_mutex\r\n");
    }

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
        if (write_mutex) xSemaphoreGive(write_mutex);
        return -1;
    }

    /* 单次 netconn_write: malloc 合并帧头+载荷，防止多任务交叉写入碎帧 */
    uint8_t *frame = (uint8_t *)malloc(hdr_len + len);
    if (!frame) {
        if (write_mutex) xSemaphoreGive(write_mutex);
        return -1;
    }
    memcpy(frame, hdr, hdr_len);
    memcpy(frame + hdr_len, text, len);
    printf("[WS] send_text call netconn_write_partly client=%p frame=%p len=%u\r\n",
           (void *)client, (void *)frame, (unsigned int)(hdr_len + len));
    size_t written = 0;
    err_t err = netconn_write_partly(client, frame, hdr_len + len, NETCONN_COPY, &written);
    printf("[WS] send_text netconn_write_partly ret=%d written=%u\r\n", (int)err, (unsigned int)written);
    free(frame);
    if (err != ERR_OK) {
        if (err == ERR_VAL) {
            printf("[WS] send_text ERR_VAL: client=%p TCP pcb dead (peer RST)\r\n", (void *)client);
        } else {
            printf("[WS] send_text netconn_write FAIL err=%d\r\n", (int)err);
        }
        if (write_mutex) xSemaphoreGive(write_mutex);
        return -1;
    }
    printf("[WS] send_text OK len=%u\r\n", (unsigned int)(hdr_len + len));
    if (write_mutex) xSemaphoreGive(write_mutex);
    printf("[WS] send_text exit success client=%p\r\n", (void *)client);
    return 0;
}

/**
 * @brief 将客户端注册到全局客户端列表中
 *
 * @param client 已完成握手的客户端netconn连接
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
 * @brief 从全局客户端列表中注销客户端
 *
 * @param client 要注销的客户端netconn连接
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
 * @brief 将消息缓存到pending队列（无客户端可用时保存）
 * @param text 消息文本（内部strdup）
 */
static void ws_pending_push(const char *text)
{
    if (!text || !s_pending_mutex) {
        return;
    }
    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    if (s_pending_count < WS_PENDING_MAX) {
        int idx = (s_pending_head + s_pending_count) % WS_PENDING_MAX;
        s_pending[idx] = strdup(text);
        if (s_pending[idx]) {
            s_pending_count++;
        }
    }
    xSemaphoreGive(s_pending_mutex);
}

/**
 * @brief 将pending队列中所有消息发送给指定客户端
 * @param client 刚完成握手的客户端netconn
 */
static void ws_pending_flush(struct netconn *client)
{
    int i;
    if (!s_pending_mutex) {
        return;
    }
    if (xSemaphoreTake(s_pending_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    int kept = 0;
    printf("[WS] pending_flush start: count=%d\r\n", s_pending_count);
    for (i = 0; i < s_pending_count; i++) {
        int idx = (s_pending_head + i) % WS_PENDING_MAX;
        if (s_pending[idx]) {
            int ret = ws_send_text(client, s_pending[idx]);
            printf("[WS] pending_flush[%d] ws_send_text ret=%d len=%u\r\n",
                   i, ret, (unsigned int)strlen(s_pending[idx]));
            if (ret == 0) {
                free(s_pending[idx]);
                s_pending[idx] = NULL;
            } else {
                kept++;
                printf("[WS] pending_flush[%d] send FAIL, retain\r\n", i);
            }
            /* 浏览器连续消息互杀typewriter: 间隔20ms让TCP分包 */
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    printf("[WS] pending_flush end: freed %d, retained %d\r\n",
           s_pending_count - kept, kept);
    /* 收缩pending队列：将未成功消息移到头部 */
    if (kept > 0) {
        int write_idx = 0;
        for (i = 0; i < s_pending_count; i++) {
            int idx = (s_pending_head + i) % WS_PENDING_MAX;
            if (s_pending[idx]) {
                s_pending[write_idx] = s_pending[idx];
                if (write_idx != idx) {
                    s_pending[idx] = NULL;
                }
                write_idx++;
            }
        }
        s_pending_head = 0;
        s_pending_count = kept;
    } else {
        s_pending_head = 0;
        s_pending_count = 0;
    }
    xSemaphoreGive(s_pending_mutex);
}

/**
 * @brief 处理单个WebSocket客户端连接的全生命周期（握手→收发消息→断开）
 *
 * @param client 客户端netconn连接
 *
 * @note 所有权约定: msg 由 malloc 分配，bus push_inbound 必须拷贝内容（不持有指针）,
 *       msg 始终在此函数内 free。bus 若存储指针而非拷贝则导致 use-after-free。
 */
static void ws_handle_client(struct netconn *client)
{
    if (!ws_do_handshake(client)) {
        netconn_close(client);
        netconn_delete(client);
        return;
    }
    AXK_LOG_INFO("[%s] WebSocket 握手OK\r\n", TAG);
    printf("[WS] handshake OK, client registered\r\n");
    ws_client_register(client);
    /* 新客户端连入，flush pending队列 */
    ws_pending_flush(client);

    /* 设置 recv/send 超时防止永久阻塞 */
    netconn_set_recvtimeout(client, 100);
    netconn_set_sendtimeout(client, 3000);  /* 原500ms, lwIP堆紧张时HTTPS占用久 */

    TickType_t last_ping = xTaskGetTickCount();

    while (s_ws_running) {
        struct netbuf *buf = NULL;
        err_t err = netconn_recv(client, &buf);
        if (err == ERR_TIMEOUT) {
            /* 每15s主动发ping保活，防止LLM工具链耗时期间浏览器断连 */
            if ((xTaskGetTickCount() - last_ping) >= pdMS_TO_TICKS(15000)) {
                ws_send_text(client, "__ping__");
                last_ping = xTaskGetTickCount();
            }
            continue;
        }
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
                    printf("[WS] recv: %s\r\n", msg);

                    /* 心跳: __ping__ 回复 __pong__ 保持连接 */
                    if (strcmp(msg, "__ping__") == 0) {
                        if (ws_send_text(client, "__pong__") != 0) {
                            AXK_LOG_WARN("[%s] send __pong__ FAILED (lwIP heap?)\r\n", TAG);
                        }
                        free(msg);
                        netbuf_delete(buf);
                        continue;
                    }
                    if (strcmp(msg, "__pong__") == 0) {
                        free(msg);
                        netbuf_delete(buf);
                        continue;
                    }

                    /* Ownership: msg is allocated here; push_inbound() strdup's
                     * the content internally before enqueuing (see
                     * axk_message_bus.c:128). The bus owns its copy; the
                     * caller retains ownership of msg and must free it. */
                    mimi_msg_t m = {0};
                    strncpy(m.channel, MIMI_CHAN_WEBSOCKET, sizeof(m.channel) - 1);
                    strncpy(m.chat_id, "ws_client", sizeof(m.chat_id) - 1);
                    m.content = msg;
                    m.priority = MIMI_PRIO_NORMAL;
                    printf("[WS] pushing to inbound...\r\n");
                    int ret = axk_message_bus_push_inbound(&m);
                    printf("[WS] push_inbound ret=%d\r\n", ret);
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
 * @brief WebSocket客户端独立任务，处理单个客户端的完整生命周期
 *
 * @param param 客户端netconn连接指针
 */
static void ws_client_task(void *param)
{
    struct netconn *client = (struct netconn *)param;
    ws_handle_client(client);
    vTaskDelete(NULL);
}

/**
 * @brief WebSocket服务器主任务，监听端口并接受客户端连接
 *
 * @param param 任务参数（未使用）
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

    err_t err;
    err = netconn_bind(listener, IP_ADDR_ANY, s_ws_port);
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[%s] netconn_bind FAIL (port %d, err=%d)\r\n", TAG, s_ws_port, err);
        netconn_delete(listener);
        s_ws_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    err = netconn_listen_with_backlog(listener, WS_LISTEN_BACKLOG);
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[%s] netconn_listen FAIL (err=%d)\r\n", TAG, err);
        netconn_close(listener);
        netconn_delete(listener);
        s_ws_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    s_ws_listener = listener;
    AXK_LOG_INFO("[%s] WebSocket service器listen port  %d\r\n", TAG, s_ws_port);

    while (s_ws_running) {
        struct netconn *client = NULL;
        err_t err = netconn_accept(listener, &client);
        if (err != ERR_OK || !client) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        AXK_LOG_INFO("[%s] 新客户端connect, spawn client task\r\n", TAG);
        if (xTaskCreate(ws_client_task, "ws_cli", WS_CLIENT_STACK, client,
                        WS_CLIENT_PRIO, NULL) != pdPASS) {
            AXK_LOG_ERROR("[%s] 创建client task FAIL, drop connection\r\n", TAG);
            netconn_close(client);
            netconn_delete(client);
        }
    }

    netconn_close(listener);
    netconn_delete(listener);
    s_ws_listener = NULL;
    s_ws_task = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 初始化WebSocket服务器模块，创建互斥锁并重置客户端列表
 *
 * @return 0成功，负数错误码
 */
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
    s_pending_mutex = xSemaphoreCreateMutex();
    {
        int i;
        for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
            s_clients[i].write_mutex = xSemaphoreCreateMutex();
        }
    }
    memset(s_pending, 0, sizeof(s_pending));
    s_pending_head = 0;
    s_pending_count = 0;
    AXK_LOG_INFO("[%s] WebSocket service器init\r\n", TAG);
    return 0;
}

/**
 * @brief 启动WebSocket服务器监听
 *
 * @param port 监听端口号，为0时使用默认端口
 * @return 0成功，-1失败（任务创建失败）
 */
int axk_ws_server_start(uint16_t port)
{
    if (s_ws_running) {
        return 0;
    }
    s_ws_port = port ? port : MIMI_WS_PORT;
    s_ws_running = true;

    if (xTaskCreate(ws_server_task, "ws_srv", 8192, NULL,
                    configMAX_PRIORITIES - 5, &s_ws_task) != pdPASS) {
        s_ws_running = false;
        return -1;
    }
    return 0;
}

/**
 * @brief 停止WebSocket服务器，关闭监听并等待任务退出
 */
void axk_ws_server_stop(void)
{
    s_ws_running = false;
    if (s_ws_listener) {
        netconn_close(s_ws_listener);
    }
    if (s_ws_task) {
        vTaskDelay(pdMS_TO_TICKS(200));  /* 等待任务退出 */
        s_ws_task = NULL;
    }
    s_ws_listener = NULL;
    AXK_LOG_INFO("[%s] WebSocket server stopped\r\n", TAG);
}

/**
 * @brief 向所有已连接的WebSocket客户端广播文本消息
 *
 * @param text 待广播的文本字符串
 * @return 至少发送给一个客户端返回0，无客户端或失败返回-1
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
            } else {
                /* netconn_write ERR_VAL → TCP PCB已死, 移出池避免阻塞pending */
                printf("[WS] send client[%d] FAIL -> cleaning up\r\n", i);
                s_clients[i].conn = NULL;
                s_clients[i].handshaked = false;
                printf("[WS] client[%d] removed from pool\r\n", i);
            }
        }
    }

    printf("[WS] send to %d clients (msg len=%u)\r\n", sent, (unsigned int)strlen(text));

    xSemaphoreGive(s_ws_mutex);

    if (sent == 0) {
        /* 无客户端在线，缓存到pending队列 */
        ws_pending_push(text);
        printf("[WS] no clients, cached to pending queue\r\n");
        /* 立即检查是否有已连接的客户端（push前刚断开的场景） */
        if (xSemaphoreTake(s_ws_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (i = 0; i < MIMI_WS_MAX_CLIENTS; i++) {
                if (s_clients[i].conn != NULL && s_clients[i].handshaked) {
                    ws_pending_flush(s_clients[i].conn);
                    break;
                }
            }
            xSemaphoreGive(s_ws_mutex);
        }
    }

    return (sent > 0) ? 0 : -1;
}
