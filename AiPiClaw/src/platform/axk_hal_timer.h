/**
 * @file axk_hal_timer.h
 * @brief 定时器硬件抽象层
 * @details 封装BL618硬件定时器，提供创建/启动/停止/删除/重置操作。
 *          BL618平台使用bflb_mtimer驱动，采用FreeRTOS软件定时器作为上层封装。
 *          支持单次和自动重载模式，回调在FreeRTOS守护任务上下文中执行。
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_TIMER_H
#define __AXK_HAL_TIMER_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 定时器句柄（不透明指针）
 *
 */
typedef void* axk_timer_handle_t;
/**
 * @brief 定时器到期回调函数签名
 *
 * @param timer 定时器句柄
 * @param arg   创建时传入的用户参数
 */
typedef void (*axk_timer_cb_t)(axk_timer_handle_t timer, void* arg);

/**
 * @brief 初始化定时器子系统
 *
 * @return 0成功，负数错误码
 */
int axk_hal_timer_init(void);
/**
 * @brief 创建一个定时器
 *
 * @param name        定时器名称（调试用）
 * @param period_ms   周期，单位毫秒
 * @param auto_reload true=自动重载（周期性），false=单次触发
 * @param cb          到期回调
 * @param arg         回调用户参数
 * @return 定时器句柄，NULL表示失败
 */
axk_timer_handle_t axk_hal_timer_create(const char* name, uint32_t period_ms, bool auto_reload, axk_timer_cb_t cb, void* arg);
/**
 * @brief 启动定时器
 *
 * @param timer 定时器句柄
 * @return 0成功，负数错误码
 */
int axk_hal_timer_start(axk_timer_handle_t timer);
/**
 * @brief 停止定时器
 *
 * @param timer 定时器句柄
 * @return 0成功，负数错误码
 */
int axk_hal_timer_stop(axk_timer_handle_t timer);
/**
 * @brief 删除定时器，释放资源
 *
 * @param timer 定时器句柄
 * @return 0成功，负数错误码
 */
int axk_hal_timer_delete(axk_timer_handle_t timer);
/**
 * @brief 重置定时器计时（重新从0开始计数）
 *
 * @param timer 定时器句柄
 * @return 0成功，负数错误码
 */
int axk_hal_timer_reset(axk_timer_handle_t timer);
/**
 * @brief 定时器轮询（用于非FreeRTOS场景下驱动软定时器）
 */
void axk_hal_timer_poll(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_TIMER_H */
