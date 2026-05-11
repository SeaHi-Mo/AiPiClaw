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

/**
 * @brief TODO: 描述axk_mimiclaw_port_uptime_ms的功能
 *
 * @return 0成功, -1失败
 */
uint32_t axk_mimiclaw_port_uptime_ms(void);
/**
 * @brief TODO: 描述axk_mimiclaw_port_sleep_ms的功能
 *
 * @param ms TODO: 描述ms
 * @return 无返回值
 */
void axk_mimiclaw_port_sleep_ms(uint32_t ms);
/**
 * @brief TODO: 描述axk_mimiclaw_port_random的功能
 *
 * @return 0成功, -1失败
 */
uint32_t axk_mimiclaw_port_random(void);
/**
 * @brief TODO: 描述axk_mimiclaw_port_time_diff的功能
 *
 * @param later TODO: 描述later
 * @param earlier TODO: 描述earlier
 * @return 0成功, -1失败
 */
uint32_t axk_mimiclaw_port_time_diff(uint32_t later, uint32_t earlier);

/* ── WebSocket 传输层 ────────────────────────── */

/**
 * @brief TODO: 描述axk_ws_connect的功能
 *
 * @param scheme TODO: 描述scheme
 * @param host TODO: 描述host
 * @param port TODO: 描述port
 * @param path TODO: 描述path
 * @return 0成功, -1失败
 */
int axk_ws_connect(const char *scheme, const char *host, uint16_t port, const char *path);
/**
 * @brief TODO: 描述axk_ws_disconnect的功能
 *
 * @return 0成功, -1失败
 */
int axk_ws_disconnect(void);
/**
 * @brief TODO: 描述axk_ws_is_connected的功能
 *
 * @return 0成功, -1失败
 */
bool axk_ws_is_connected(void);
/**
 * @brief TODO: 描述axk_ws_send_text的功能
 *
 * @param text TODO: 描述text
 * @return 0成功, -1失败
 */
int axk_ws_send_text(const char *text);
/**
 * @brief TODO: 描述axk_ws_send_binary的功能
 *
 * @param data TODO: 描述data
 * @param len TODO: 描述len
 * @return 0成功, -1失败
 */
int axk_ws_send_binary(const void *data, size_t len);

typedef void (*axk_ws_text_handler_t)(const char *text, size_t len, bool is_finished);
/**
 * @brief TODO: 描述axk_ws_set_text_handler的功能
 *
 * @param handler TODO: 描述handler
 * @return 无返回值
 */
void axk_ws_set_text_handler(axk_ws_text_handler_t handler);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MIMICLAW_PORT_H */
