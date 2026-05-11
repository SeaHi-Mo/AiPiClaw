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

/* @brief TODO: 描述axk_gpio_policy_init的功能 @return 0成功, -1失败 */
int axk_gpio_policy_init(void);
/* @brief TODO: 描述axk_gpio_policy_check的功能 @param pin TODO: 描述pin @param action TODO: 描述action @return 0成功, -1失败 */
int axk_gpio_policy_check(uint8_t pin, const char *action);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_POLICY_H */
