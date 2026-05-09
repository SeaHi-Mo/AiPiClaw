/**
 * @file axk_hal_gpio.h
 * @brief GPIO硬件抽象层 - 宏元编程实现平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note via 宏元编程will not 同平台GPIO API统一为平台无关接口
 */

#ifndef __AXK_HAL_GPIO_H
#define __AXK_HAL_GPIO_H

#include "axk_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * GPIOdevice handle 
 * ============================================================ */
struct axk_gpio_dev;
typedef struct axk_gpio_dev* axk_gpio_handle_t;

/* ============================================================
 * GPIOconfig结构体 - 平台无关
 * ============================================================ */
typedef struct {
    uint32_t pin;           /**< GPIOpin 号 */
    uint32_t mode;          /**< 工作mode  */
    uint32_t pull;          /**< 上下拉config */
    uint32_t drive;         /**< 驱动能力 */
    void*    priv;          /**< 平台private data */
} axk_gpio_cfg_t;

/* GPIO工作mode  */
#define AXK_GPIO_MODE_IN            0x01
#define AXK_GPIO_MODE_OUT           0x02
#define AXK_GPIO_MODE_OD            0x03  /**< 开漏output  */
#define AXK_GPIO_MODE_AF            0x04  /**< 复用功能 */

/* GPIO上下拉 */
#define AXK_GPIO_PULL_NONE          0x00
#define AXK_GPIO_PULL_UP            0x01
#define AXK_GPIO_PULL_DOWN          0x02

/* GPIO驱动能力 */
#define AXK_GPIO_DRIVE_WEAK         0x00
#define AXK_GPIO_DRIVE_STRONG       0x01

/* GPIO电平 */
#define AXK_GPIO_LOW                0
#define AXK_GPIO_HIGH               1

/* ============================================================
 * GPIO操作func  - 平台无关接口
 * ============================================================ */

/**
 * @brief initGPIOmodule
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_hal_gpio_init(void);
int axk_hal_gpio_set_direction(uint32_t pin, uint32_t mode);

/**
 * @brief configGPIOpin 
 * @param[in] pin pin 号
 * @param[in] cfg configparam 
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_hal_gpio_config(uint32_t pin, const axk_gpio_cfg_t* cfg);

/**
 * @brief set GPIOoutput 电平
 * @param[in] pin pin 号
 * @param[in] level 电平（0 or 1）
 * @return OKreturn 0
 */
int axk_hal_gpio_set_level(uint32_t pin, uint32_t level);

/**
 * @brief get GPIOinput 电平
 * @param[in] pin pin 号
 * @return 电平value （0 or 1），ERRORreturn -1
 */
int axk_hal_gpio_get_level(uint32_t pin);

/**
 * @brief 翻转GPIO电平
 * @param[in] pin pin 号
 * @return OKreturn 0
 */
int axk_hal_gpio_toggle(uint32_t pin);

/**
 * @brief set GPIO断callback 
 * @param[in] pin pin 号
 * @param[in] trigger trigger 方式
 * @param[in] cb callback func 
 * @param[in] arg userparam 
 * @return OKreturn 0
 */
typedef void (*axk_gpio_isr_cb_t)(uint32_t pin, void* arg);

#define AXK_GPIO_INTR_POSEDGE       0x01
#define AXK_GPIO_INTR_NEGEDGE       0x02
#define AXK_GPIO_INTR_ANYEDGE       0x03
#define AXK_GPIO_INTR_LOW_LEVEL     0x04
#define AXK_GPIO_INTR_HIGH_LEVEL    0x05

int axk_hal_gpio_set_interrupt(uint32_t pin, uint32_t trigger, axk_gpio_isr_cb_t cb, void* arg);

/**
 * @brief enabledGPIO断
 * @param[in] pin pin 号
 * @return OKreturn 0
 */
int axk_hal_gpio_intr_enable(uint32_t pin);

/**
 * @brief disable GPIO断
 * @param[in] pin pin 号
 * @return OKreturn 0
 */
int axk_hal_gpio_intr_disable(uint32_t pin);

/**
 * @brief set GPIO上下拉
 * @param[in] pin pin 号
 * @param[in] pull AXK_GPIO_PULL_UP/DOWN/NONE
 * @return OKreturn 0
 */
int axk_hal_gpio_set_pull(uint32_t pin, uint32_t pull);

/**
 * @brief get GPIO上下拉status 
 * @param[in] pin pin 号
 * @return 上下拉type 
 */
int axk_hal_gpio_get_pull(uint32_t pin);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_HAL_GPIO_H */
