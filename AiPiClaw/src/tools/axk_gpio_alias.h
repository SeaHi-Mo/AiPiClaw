/**
 * @file axk_gpio_alias.h
 * @brief GPIO 别名注册表 - 将语义化名称映射到物理引脚
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 提供别名解析、注册、列表功能，供 LLM 工具层使用
 */

#ifndef __AXK_GPIO_ALIAS_H
#define __AXK_GPIO_ALIAS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXK_GPIO_ALIAS_NAME_MAX  24   /**< 别名最大长度 */
#define AXK_GPIO_ALIAS_DESC_MAX  64   /**< 描述最大长度 */
#define AXK_GPIO_ALIAS_MAX       32   /**< 最大注册别名数量 */

/* 别名权限标志 */
#define AXK_GPIO_ALIAS_FLAG_WRITE   (1 << 0)  /**< 允许写入 */
#define AXK_GPIO_ALIAS_FLAG_READ    (1 << 1)  /**< 允许读取 */
#define AXK_GPIO_ALIAS_FLAG_PIN_OK  (1 << 2)  /**< 引脚已通过白名单校验 */

/**
 * @brief GPIO 别名条目结构体
 */
typedef struct {
    char name[AXK_GPIO_ALIAS_NAME_MAX];       /**< 别名（如 "green_led"） */
    char description[AXK_GPIO_ALIAS_DESC_MAX]; /**< 描述（如 "绿灯(GPIO14,高电平)"） */
    uint8_t pin;                               /**< 实际GPIO引脚号 */
    uint8_t active_level;                      /**< 有效电平（1=高电平，0=低电平） */
    uint8_t flags;                             /**< 权限标志位 */
} axk_gpio_alias_t;

/**
 * @brief 初始化别名注册表，注册默认项
 * @return 0 成功，-1 失败
 */
int axk_gpio_alias_init(void);

/**
 * @brief 注册单个别名
 * @param[in] alias 别名条目指针
 * @return 0 成功，-1 注册表已满或参数无效
 */
int axk_gpio_alias_register(const axk_gpio_alias_t *alias);

/**
 * @brief 通过名称解析别名
 * @param[in] name 别名名称
 * @param[out] out 解析结果输出
 * @return 0 成功找到，-1 未找到
 */
int axk_gpio_alias_resolve(const char *name, axk_gpio_alias_t *out);

/**
 * @brief 获取已注册别名数量
 * @return 别名数量
 */
int axk_gpio_alias_get_count(void);

/**
 * @brief 列出所有已注册别名的文本
 * @param[out] buf 输出缓冲区
 * @param[in] size 缓冲区大小
 * @return 写入的字符数
 */
int axk_gpio_alias_list(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_ALIAS_H */
