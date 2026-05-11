/**
 * @file axk_transport_ws.c
 * @brief WebSocket 传输层 - 基于SDK librws 客户端实现
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 整合自官方 solution/mimiclaw/port/src/mimiclaw_transport_ws.c
 *       use SDKbuiltin  librws 库，稳定可靠
 */

#include "axk_mimiclaw_port.h"

#include <librws.h>
#include <rws_error.h>
#include "task.h"

#define DBG_TAG "AXK_WS"
#include "log.h"

static rws_socket s_socket;
static volatile bool s_connected;
static axk_ws_text_handler_t s_text_handler;

/* ── callback func  ───────────────────────────────────────── */

/**
 * @brief WebSocket连接成功回调
 * @param socket WebSocket套接字
 */
static void on_socket_connected(rws_socket socket)
{
    (void)socket;
    s_connected = true;
    LOG_I("websocket connected\r\n");
}

/**
 * @brief WebSocket断开连接回调
 * @param socket WebSocket套接字
 */
static void on_socket_disconnected(rws_socket socket)
{
    rws_error error = rws_socket_get_error(socket);
    s_connected = false;

    if (error) {
        LOG_W("websocket disconnected code=%d desc=%s\r\n",
              rws_error_get_code(error),
              rws_error_get_description(error));
    } else {
        LOG_W("websocket disconnected\r\n");
    }
}

/**
 * @brief WebSocket接收文本消息回调
 * @param socket WebSocket套接字
 * @param text 接收到的文本数据
 * @param length 文本长度
 * @param is_finished 是否为最后一帧
 */
static void on_socket_received_text(rws_socket socket, const char *text,
                                     const unsigned int length, bool is_finished)
{
    (void)socket;

    if (!text || length == 0) {
        return;
    }

    if (length <= 200) {
        LOG_I("websocket recv fin=%d len=%u: %.*s\r\n",
              (int)is_finished, length, (int)length, text);
    } else {
        LOG_I("websocket recv fin=%d len=%u: %.*s...(truncated)\r\n",
              (int)is_finished, length, 200, text);
    }

    if (s_text_handler) {
        s_text_handler(text, (size_t)length, is_finished);
    }
}

/* ── public  API ────────────────────────────────────────── */

/**
 * @brief 建立 WebSocket connect
 * @param[in] scheme 协议 (ws  or  wss)
 * @param[in] host service器addr 
 * @param[in] port port 号
 * @param[in] path path 
 * @return OKreturn 0，FAILreturn 负value 
 */
int axk_ws_connect(const char *scheme, const char *host, uint16_t port, const char *path)
{
    rws_error err;
    uint32_t t0;

    if (!scheme || !host || !path) {
        return -1;
    }

    axk_ws_disconnect();

    s_socket = rws_socket_create();
    if (!s_socket) {
        return -2;
    }

    rws_socket_set_host(s_socket, host);
    rws_socket_set_port(s_socket, (int)port);
    rws_socket_set_path(s_socket, path);
    rws_socket_set_scheme(s_socket, scheme);
    rws_socket_set_on_connected(s_socket, on_socket_connected);
    rws_socket_set_on_disconnected(s_socket, on_socket_disconnected);
    rws_socket_set_on_received_text(s_socket, on_socket_received_text);

    s_connected = false;

    if (!rws_socket_connect(s_socket)) {
        axk_ws_disconnect();
        return -3;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        t0 = axk_mimiclaw_port_uptime_ms();
        while (!s_connected && (axk_mimiclaw_port_uptime_ms() - t0 < 5000)) {
            axk_mimiclaw_port_sleep_ms(20);
        }
        if (s_connected) {
            return 0;
        }

        err = rws_socket_get_error(s_socket);
        if (err) {
            LOG_W("websocket connect timeout code=%d desc=%s\r\n",
                  rws_error_get_code(err),
                  rws_error_get_description(err));
        } else {
            LOG_W("websocket connect timeout without detailed error\r\n");
        }
        return -4;
    }

    return 0;
}

/**
 * @brief disconnect WebSocket connect and release 资源
 * @return OKreturn 0
 */
int axk_ws_disconnect(void)
{
    if (s_socket) {
        rws_socket_disconnect_and_release(s_socket);
        s_socket = NULL;
    }

    s_connected = false;
    return 0;
}

/**
 * @brief check  WebSocket is否connect
 * @return connectreturn true
 */
bool axk_ws_is_connected(void)
{
    return (s_socket != NULL) && (rws_socket_is_connected(s_socket) == rws_true) && s_connected;
}

/**
 * @brief send文本msg
 * @param[in] text 要send文本（null-terminated）
 * @return OKreturn 0，FAILreturn -1
 */
int axk_ws_send_text(const char *text)
{
    if (!text || !axk_ws_is_connected()) {
        return -1;
    }

    return rws_socket_send_text(s_socket, text) ? 0 : -1;
}

/**
 * @brief send二进制data（auto 分片）
 * @param[in] data dataptr 
 * @param[in] len datalength 
 * @return OKreturn 0，FAILreturn -1
 */
int axk_ws_send_binary(const void *data, size_t len)
{
    const uint8_t *bin = (const uint8_t *)data;
    size_t first;
    size_t second;
    size_t third;

    if (!data || len == 0 || !axk_ws_is_connected()) {
        return -1;
    }

    if (len == 1) {
        return rws_socket_send_bin_finish(s_socket, (const char *)bin, len) ? 0 : -1;
    }

    if (len == 2) {
        return (rws_socket_send_bin_start(s_socket, (const char *)&bin[0], 1) &&
                rws_socket_send_bin_finish(s_socket, (const char *)&bin[1], 1)) ? 0 : -1;
    }

    first = len / 3;
    if (first == 0) {
        first = 1;
    }
    second = len / 3;
    if (second == 0) {
        second = 1;
    }
    third = len - first - second;

    if (third == 0) {
        second -= 1;
        third = 1;
    }

    return (rws_socket_send_bin_start(s_socket, (const char *)&bin[0], first) &&
            rws_socket_send_bin_continue(s_socket, (const char *)&bin[first], second) &&
            rws_socket_send_bin_finish(s_socket, (const char *)&bin[first + second], third)) ? 0 : -1;
}

/**
 * @brief set 文本msgcallback process器
 * @param[in] handler callback func 
 */
void axk_ws_set_text_handler(axk_ws_text_handler_t handler)
{
    s_text_handler = handler;
}
