/**
 * @file axk_gpio_policy.h
 * @brief gpio_policy module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_GPIO_POLICY_H
#define __AXK_GPIO_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化GPIO安全策略模块
 * @return 0成功，负数错误码
 */
int axk_gpio_policy_init(void);
/**
 * @brief 检查GPIO操作是否允许
 * @param[in] pin GPIO引脚号
 * @param[in] action 操作类型（"read"/"write"）
 * @return 0允许，非零拒绝
 */
int axk_gpio_policy_check(uint8_t pin, const char *action);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_POLICY_H */
