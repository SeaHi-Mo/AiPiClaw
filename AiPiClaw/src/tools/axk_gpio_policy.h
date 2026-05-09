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

int axk_gpio_policy_init(void);
int axk_gpio_policy_check(uint8_t pin, const char *action);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_GPIO_POLICY_H */
