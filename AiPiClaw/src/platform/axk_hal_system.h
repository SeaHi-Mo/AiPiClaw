/**
 * @file axk_hal_system.h
 * @brief system级硬件抽象层
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_SYSTEM_H
#define __AXK_HAL_SYSTEM_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

int axk_hal_system_init(void);
void axk_hal_system_reset(void);
uint32_t axk_hal_system_get_time_ms(void);
uint32_t axk_hal_system_get_time_us(void);
uint32_t axk_hal_system_get_free_heap(void);
const char* axk_hal_system_get_chip_name(void);
const char* axk_hal_system_get_sdk_version(void);
void axk_hal_system_enter_critical(void);
void axk_hal_system_exit_critical(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_SYSTEM_H */
