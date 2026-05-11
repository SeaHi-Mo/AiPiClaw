/**
 * @file axk_hal_system.h
 * @brief 系统级硬件抽象层
 * @details 封装芯片复位、时间获取、堆内存查询、临界区保护等系统级操作。
 *          BL618平台：复位通过NVIC_SystemReset()，时间通过bflb_mtimer，
 *          堆查询通过kfree_size(0)（BL618用heap_3/kmalloc，非标准FreeRTOS heap_4），
 *          临界区通过taskENTER_CRITICAL()实现。
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_SYSTEM_H
#define __AXK_HAL_SYSTEM_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化系统HAL层
 *
 * @return 0成功，负数错误码
 */
int axk_hal_system_init(void);
/**
 * @brief 软件复位芯片
 *
 * @note BL618：调用NVIC_SystemReset()，立即复位不返回
 */
void axk_hal_system_reset(void);
/**
 * @brief 获取系统运行时间（毫秒）
 *
 * @return 自启动以来的毫秒数
 */
uint32_t axk_hal_system_get_time_ms(void);
/**
 * @brief 获取系统运行时间（微秒）
 *
 * @return 自启动以来的微秒数
 */
uint32_t axk_hal_system_get_time_us(void);
/**
 * @brief 获取空闲堆内存大小
 *
 * @return 空闲堆字节数
 * @note BL618平台：使用kfree_size(0)而非xPortGetFreeHeapSize()，
 *       因为BL618 SDK使用heap_3（内部调用kmalloc/kfree），
 *       xPortGetFreeHeapSize()是弱符号stub始终返回0。
 */
uint32_t axk_hal_system_get_free_heap(void);
/**
 * @brief 获取芯片名称
 *
 * @return 芯片型号字符串（如"BL618"）
 */
const char* axk_hal_system_get_chip_name(void);
/**
 * @brief 获取SDK版本号
 *
 * @return SDK版本字符串
 */
const char* axk_hal_system_get_sdk_version(void);
/**
 * @brief 进入临界区（关中断）
 *
 * @note 配对axk_hal_system_exit_critical()使用
 */
void axk_hal_system_enter_critical(void);
/**
 * @brief 退出临界区（恢复中断）
 *
 * @note 配对axk_hal_system_enter_critical()使用
 */
void axk_hal_system_exit_critical(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_SYSTEM_H */
