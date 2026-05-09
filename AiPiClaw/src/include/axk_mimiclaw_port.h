/**
 * @file axk_mimiclaw_port.h
 * @brief 平台通用接口 - 安信可科技 BL618 port
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_MIMICLAW_PORT_H
#define __AXK_MIMICLAW_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── 平台基础func  ───────────────────────────── */

uint32_t axk_mimiclaw_port_uptime_ms(void);
void axk_mimiclaw_port_sleep_ms(uint32_t ms);
uint32_t axk_mimiclaw_port_random(void);
uint32_t axk_mimiclaw_port_time_diff(uint32_t later, uint32_t earlier);

/* ── WebSocket 传输层 ────────────────────────── */

int axk_ws_connect(const char *scheme, const char *host, uint16_t port, const char *path);
int axk_ws_disconnect(void);
bool axk_ws_is_connected(void);
int axk_ws_send_text(const char *text);
int axk_ws_send_binary(const void *data, size_t len);

typedef void (*axk_ws_text_handler_t)(const char *text, size_t len, bool is_finished);
void axk_ws_set_text_handler(axk_ws_text_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MIMICLAW_PORT_H */
