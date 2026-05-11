/**
 * @file axk_gpio_policy.h
 * @brief GPIO 引脚权限策略 - 白名单机制
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_GPIO_POLICY_H
#define __AXK_GPIO_POLICY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXK_GPIO_POLICY_MAX_PINS  32  /**< 白名单最大引脚数 */

/**
 * @brief 初始化GPIO安全策略
 * @return 0 成功，-1 失败
 */
int axk_gpio_policy_init(void);

/**
 * @brief 检查指定引脚和操作是否被允许
 * @param[in] pin GPIO引脚号
 * @param[in] action 操作名称（如 "set_level" / "get_level" / "set_direction"）
 * @return 0 允许，-1 拒绝
 */
int axk_gpio_policy_check(uint8_t pin, const char *action);

/**
 * @brief 将引脚加入白名单
 * @param[in] pin GPIO引脚号
 * @return 0 成功，-1 失败
 */
int axk_gpio_policy_allow(uint8_t pin);

/**
 * @brief 从白名单移除引脚
 * @param[in] pin GPIO引脚号
 * @return 0 成功，-1 失败
 */
int axk_gpio_policy_deny(uint8_t pin);

/**
 * @brief 检查引脚是否在白名单中
 * @param[in] pin GPIO引脚号
 * @return true 在白名单中，false 不在
 */
bool axk_gpio_policy_is_allowed(uint8_t pin);

/**
 * @brief 获取白名单引脚列表文本
 * @param[out] buf 输出缓冲区
 * @param[in] size 缓冲区大小
 * @return 写入的字符数
 */
int axk_gpio_policy_list(char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_POLICY_H */
