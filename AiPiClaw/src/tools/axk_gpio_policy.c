/**
 * @file axk_gpio_policy.c
 * @brief GPIO pin 权限策略 - 白名单机制
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 定义哪些pin 允许 GPIO 控制module操作
 *       BL616DK 开发板可用 GPIO: PIN 10,11,12,17（避开 UART1/I2C0/SPI0）
 */

#include "axk_gpio_policy.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>

/* 允许操作pin 白名单 */
static const uint8_t ALLOWED_PINS[] = {
    10,   /**< KEY_0 */
    11,   /**< KEY_1 (& I2C0_SDA 共用, 二选一) */
    12,   /**< KEY_2 (& SPI0_CS 共用, 二选一) */
    17,   /**< TEST_PIN */
};
#define ALLOWED_COUNT (sizeof(ALLOWED_PINS) / sizeof(ALLOWED_PINS[0]))

/* 允许操作 */
static const char *ALLOWED_ACTIONS[] = {
    "set_level",
    "get_level",
    "set_direction",
};
#define ACTION_COUNT (sizeof(ALLOWED_ACTIONS) / sizeof(ALLOWED_ACTIONS[0]))

static bool s_initialized = false;

/* ── public  API ────────────────────────────────────── */

/**
 * @brief TODO: 描述axk_gpio_policy_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_gpio_policy_init(void)
{
    if (s_initialized) return 0;

    s_initialized = true;
    AXK_LOG_INFO("[axk_gpio_policy] 策略initok, "
                 "允许%dpin , %d操作type \r\n",
                 (int)ALLOWED_COUNT, (int)ACTION_COUNT);
    return 0;
}

/**
 * @brief TODO: 描述axk_gpio_policy_check的功能
 *
 * @param pin TODO: 描述pin
 * @param action TODO: 描述action
 * @return 0成功, -1失败
 */
int axk_gpio_policy_check(uint8_t pin, const char *action)
{
    size_t i;
    bool pin_ok = false;
    bool action_ok = false;

    if (!s_initialized) return -1;
    if (!action) return -1;

    /* check pin is否in 白名单 */
    for (i = 0; i < ALLOWED_COUNT; i++) {
        if (ALLOWED_PINS[i] == pin) {
            pin_ok = true;
            break;
        }
    }

    if (!pin_ok) {
        AXK_LOG_WARN("[axk_gpio_policy] pin  %d not in 白名单\r\n", pin);
        return -1;
    }

    /* check 操作is否允许 */
    for (i = 0; i < ACTION_COUNT; i++) {
        if (strcmp(ALLOWED_ACTIONS[i], action) == 0) {
            action_ok = true;
            break;
        }
    }

    if (!action_ok) {
        AXK_LOG_WARN("[axk_gpio_policy] 操作 '%s' not 允许\r\n", action);
        return -1;
    }

    return 0;
}
