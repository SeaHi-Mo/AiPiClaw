/**
 * @file axk_gpio_control.h
 * @brief gpio_control module - IO8key ：短按唤醒 / 长按restore 出厂
 * @version 1.1
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_GPIO_CONTROL_H
#define __AXK_GPIO_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * key config
 * ============================================================ */
#define AXK_FACTORY_RESET_BTN_PIN        14      /**< IO14: key (GPIO8被PSRAM占用) */
#define AXK_FACTORY_RESET_HOLD_MS        3000    /**< 长按阈value : 3s */

/* ============================================================
 * key status 机
 * ============================================================ */
typedef enum {
    AXK_BTN_IDLE = 0,           /**< empty 闲（not 按下） */
    AXK_BTN_PRESSING,           /**< 正in 按下（计时） */
    AXK_BTN_FACTORY_RESET,      /**< trigger restore 出厂 */
} axk_btn_state_t;

/* ============================================================
 * public 接口
 * ============================================================ */

/**
 * @brief initGPIO ctrlmodule（IO8key config）
 * @return OKreturn 0
 */
int axk_gpio_control_init(void);

/**
 * @brief key poll （需in main loop周期call ）
 * @note per 10mscall 一次，internalstatus 机detect 长按
 */
void axk_gpio_control_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_CONTROL_H */
