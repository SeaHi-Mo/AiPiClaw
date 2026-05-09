/**
 * @file axk_hal_timer.h
 * @brief timer硬件抽象层
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_TIMER_H
#define __AXK_HAL_TIMER_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef void* axk_timer_handle_t;
typedef void (*axk_timer_cb_t)(axk_timer_handle_t timer, void* arg);

int axk_hal_timer_init(void);
axk_timer_handle_t axk_hal_timer_create(const char* name, uint32_t period_ms, bool auto_reload, axk_timer_cb_t cb, void* arg);
int axk_hal_timer_start(axk_timer_handle_t timer);
int axk_hal_timer_stop(axk_timer_handle_t timer);
int axk_hal_timer_delete(axk_timer_handle_t timer);
int axk_hal_timer_reset(axk_timer_handle_t timer);
void axk_hal_timer_poll(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_TIMER_H */
