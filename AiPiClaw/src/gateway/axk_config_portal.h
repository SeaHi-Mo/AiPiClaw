/**
 * @file axk_config_portal.h
 * @brief Config Portal module - SoftAP HTTP配置门户
 * @version 1.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note 提供 SoftAP 模式下 Web 配置页面（配WiFi+LLM），
 *       基于 lwIP netconn 实现 HTTP 路由框架。
 *       所有 API 返回 JSON。
 */
#ifndef __AXK_CONFIG_PORTAL_H
#define __AXK_CONFIG_PORTAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化配置门户模块
 *
 * 注册路由表，准备 HTTP 服务器内部状态。
 * 不启动监听，需调用 axk_config_portal_start() 启动。
 */
void axk_config_portal_init(void);

/**
 * @brief 启动配置门户 HTTP 服务器
 *
 * 在 MIMI_ONBOARD_HTTP_PORT (80) 上启动监听，
 * 处理配置页面和 REST API 请求。
 *
 * @return 0成功，负数错误码
 */
int axk_config_portal_start(void);

/**
 * @brief 停止配置门户 HTTP 服务器
 *
 * 关闭监听socket，释放资源。
 */
void axk_config_portal_stop(void);

/**
 * @brief 检查配置门户是否正在运行
 *
 * @return true 运行中，false 已停止
 */
bool axk_config_portal_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_CONFIG_PORTAL_H */
