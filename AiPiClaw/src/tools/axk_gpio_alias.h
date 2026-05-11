/**
 * @file axk_gpio_alias.h
 * @brief GPIO别名注册表 - 将逻辑名称映射到物理引脚
 * @version 1.0
 * @date 2026-05-11
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 支持中文别名如"绿灯"→GPIO14，"on"/"off"自动映射为高/低电平
 *       每个别名可指定读写权限标志
 */

#ifndef __AXK_GPIO_ALIAS_H
#define __AXK_GPIO_ALIAS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 别名权限标志位 */
#define AXK_ALIAS_FLAG_WRITE   (1 << 0)  /**< 允许写操作(set level) */
#define AXK_ALIAS_FLAG_READ    (1 << 1)  /**< 允许读操作(get level) */
#define AXK_ALIAS_FLAG_CONFIRM (1 << 2)  /**< 操作需用户确认 */

/** GPIO别名条目 */
typedef struct {
    const char *name;            /**< 别名: "green_led", "绿灯", "led_g" */
    const char *description;     /**< 描述: "绿灯(GPIO14,高电平有效)" */
    uint8_t     pin;             /**< 实际GPIO引脚号 */
    uint8_t     active_level;    /**< 有效电平: 1=高电平有效(亮=1), 0=低电平有效(亮=0) */
    uint8_t     flags;           /**< 权限标志: AXK_ALIAS_FLAG_* 位组合 */
} axk_gpio_alias_t;

/**
 * @brief 初始化别名注册表
 * @note 注册板载LED(12/14/15)和按键(10/11)的默认别名
 * @return 0 成功
 */
int axk_gpio_alias_init(void);

/**
 * @brief 按名称查找别名
 * @param[in] name 别名（支持中文如"绿灯"）
 * @param[out] out 输出匹配的别名条目
 * @return 0 找到，-1 未找到
 */
int axk_gpio_alias_resolve(const char *name, axk_gpio_alias_t *out);

/**
 * @brief 列出所有已注册别名
 * @param[out] buf 输出缓冲区
 * @param[in] size 缓冲区大小
 * @return 输出的字符数
 */
int axk_gpio_alias_list(char *buf, size_t size);

/**
 * @brief 动态注册一个别名
 * @param[in] alias 别名条目指针
 * @return 0 成功，-1 表满或参数无效
 */
int axk_gpio_alias_register(const axk_gpio_alias_t *alias);

/**
 * @brief 获取别名注册表当前条目数
 * @return 条目数
 */
int axk_gpio_alias_count(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_ALIAS_H */
