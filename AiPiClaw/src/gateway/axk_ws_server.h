/**
 * @file axk_ws_server.h
 * @brief ws_server module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_WS_SERVER_H
#define __AXK_WS_SERVER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化WebSocket服务器模块
 *
 * @return 0成功，负数错误码
 */
int axk_ws_server_init(void);
/**
 * @brief 启动WebSocket服务器监听
 *
 * @param[in] port 监听端口号
 * @return 0成功，负数错误码
 */
int axk_ws_server_start(uint16_t port);

/**
 * @brief 停止WebSocket服务器
 */
void axk_ws_server_stop(void);

/**
 * @brief  to current connect WebSocket 客户端send文本msg
 *
 * @param[in] text 要send文本
 * @return OKreturn 0，无客户端connect or FAILreturn 非零
 */
int axk_ws_server_send(const char *text);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_WS_SERVER_H */
