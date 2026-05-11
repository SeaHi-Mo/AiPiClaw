/**
 * @file axk_hal_gpio.h
 * @brief GPIO硬件抽象层 - 宏元编程实现平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 通过条件编译实现BL618与ESP32的GPIO API统一为平台无关接口
 */

#ifndef __AXK_HAL_GPIO_H
#define __AXK_HAL_GPIO_H

#include "axk_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * GPIO设备句柄
 * ============================================================ */
struct axk_gpio_dev;
typedef struct axk_gpio_dev* axk_gpio_handle_t;

/* ============================================================
 * GPIO配置结构体 - 平台无关
 * ============================================================ */
typedef struct {
    uint32_t pin;           /**< GPIO引脚号 */
    uint32_t mode;          /**< 工作模式（IN/OUT/OD/AF） */
    uint32_t pull;          /**< 上下拉配置 */
    uint32_t drive;         /**< 驱动能力 */
    void*    priv;          /**< 平台私有数据 */
} axk_gpio_cfg_t;

/* GPIO工作模式 */
#define AXK_GPIO_MODE_IN            0x01
#define AXK_GPIO_MODE_OUT           0x02
#define AXK_GPIO_MODE_OD            0x03  /**< 开漏输出 */
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
 * GPIO操作函数 - 平台无关接口
 * ============================================================ */

/**
 * @brief 初始化GPIO硬件抽象层
 *
 * @return 0 成功，非零 失败
 */
int axk_hal_gpio_init(void);
/**
 * @brief 设置GPIO引脚方向
 *
 * @param[in] pin GPIO引脚号
 * @param[in] mode 引脚模式（AXK_GPIO_MODE_IN / OUT / OD / AF）
 * @return 0 成功
 */
int axk_hal_gpio_set_direction(uint32_t pin, uint32_t mode);

/**
 * @brief 配置GPIO引脚（模式、上下拉、驱动能力）
 *
 * @param[in] pin GPIO引脚号
 * @param[in] cfg 配置结构体指针
 * @return 0 成功，非零 失败
 */
int axk_hal_gpio_config(uint32_t pin, const axk_gpio_cfg_t* cfg);

/**
 * @brief 设置GPIO输出电平
 *
 * @param[in] pin GPIO引脚号
 * @param[in] level 电平值（0=低电平，1=高电平）
 * @return 0 成功
 */
int axk_hal_gpio_set_level(uint32_t pin, uint32_t level);

/**
 * @brief 读取GPIO输入电平
 *
 * @param[in] pin GPIO引脚号
 * @return 1 高电平，0 低电平，-1 失败
 */
int axk_hal_gpio_get_level(uint32_t pin);

/**
 * @brief 翻转GPIO电平
 *
 * @param[in] pin GPIO引脚号
 * @return 0 成功
 */
int axk_hal_gpio_toggle(uint32_t pin);

/**
 * @brief GPIO中断回调函数类型定义
 *
 * @param[in] pin 触发中断的引脚号
 * @param[in] arg 用户自定义参数
 */
typedef void (*axk_gpio_isr_cb_t)(uint32_t pin, void* arg);

#define AXK_GPIO_INTR_POSEDGE       0x01
#define AXK_GPIO_INTR_NEGEDGE       0x02
#define AXK_GPIO_INTR_ANYEDGE       0x03
#define AXK_GPIO_INTR_LOW_LEVEL     0x04
#define AXK_GPIO_INTR_HIGH_LEVEL    0x05

/**
 * @brief 设置GPIO引脚中断回调
 *
 * @param[in] pin GPIO引脚号
 * @param[in] trigger 触发方式（POSEDGE/NEGEDGE/ANYEDGE/LOW_LEVEL/HIGH_LEVEL）
 * @param[in] cb 中断回调函数
 * @param[in] arg 回调用户参数
 * @return 0 成功
 */
int axk_hal_gpio_set_interrupt(uint32_t pin, uint32_t trigger, axk_gpio_isr_cb_t cb, void* arg);

/**
 * @brief 使能GPIO引脚中断
 *
 * @param[in] pin GPIO引脚号
 * @return 0 成功
 */
int axk_hal_gpio_intr_enable(uint32_t pin);

/**
 * @brief 禁能GPIO引脚中断
 *
 * @param[in] pin GPIO引脚号
 * @return 0 成功
 */
int axk_hal_gpio_intr_disable(uint32_t pin);

/**
 * @brief 设置GPIO上下拉电阻
 *
 * @param[in] pin GPIO引脚号
 * @param[in] pull 上下拉模式（AXK_GPIO_PULL_UP / DOWN / NONE）
 * @return 0 成功
 */
int axk_hal_gpio_set_pull(uint32_t pin, uint32_t pull);

/**
 * @brief 获取GPIO上下拉配置
 *
 * @param[in] pin GPIO引脚号
 * @return 上下拉类型
 */
int axk_hal_gpio_get_pull(uint32_t pin);

/** @brief 修复BL618 GPIO OE寄存器Bug，手动设置OE位到GLB GPIO CFG寄存器
 *  @param[in] pin GPIO引脚号 */
void axk_gpio_fix_oe(uint8_t pin);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_HAL_GPIO_H */
