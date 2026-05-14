/**
 * @file axk_gpio_policy.c
 * @brief GPIO 引脚权限策略 - 动态白名单机制
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 管理哪些引脚允许GPIO操作，支持运行时动态增删白名单
 *       AiPi-Eyes-DU 默认白名单: GPIO10(KEY0), 11(KEY1), 12(LED_R), 14(LED_G), 15(LED_B)
 */

#include "axk_gpio_policy.h"
#include "axk_platform.h"
#include "axk_board_config.h"
#include "shell.h"
#include "FreeRTOS.h"
#include "semphr.h"

#include <stdio.h>
#include <string.h>

/* ── 动态白名单 ──────────────────────────────────── */

static uint8_t s_allowed_pins[AXK_GPIO_POLICY_MAX_PINS];
static int s_allowed_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

/* 允许的操作类型 */
static const char *ALLOWED_ACTIONS[] = {
    "set_level",
    "get_level",
    "set_direction",
};
#define ACTION_COUNT (sizeof(ALLOWED_ACTIONS) / sizeof(ALLOWED_ACTIONS[0]))

static bool s_initialized = false;

/**
 * @brief 注册默认白名单引脚（从 board.json 解析结果加载）
 */
static void axk_register_default_pins(void)
{
    int pins[AXK_GPIO_POLICY_MAX_PINS];
    int count = axk_board_config_get_allowed_pins(pins, AXK_GPIO_POLICY_MAX_PINS);

    if (count == 0) {
        /* Fallback: AiPi-Eyes-DU 板载可用引脚 */
        const uint8_t defaults[] = { 10, 11, 12, 14, 15 };
        size_t i;
        for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
            if (s_allowed_count < AXK_GPIO_POLICY_MAX_PINS) {
                s_allowed_pins[s_allowed_count++] = defaults[i];
            }
        }
        return;
    }

    /* 从 board_config 填充白名单 */
    for (int i = 0; i < count && s_allowed_count < AXK_GPIO_POLICY_MAX_PINS; i++) {
        s_allowed_pins[s_allowed_count++] = (uint8_t)pins[i];
    }
}

int axk_gpio_policy_init(void)
{
    if (s_initialized) return 0;

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        AXK_LOG_ERROR("[gpio_policy] 创建互斥锁失败\r\n");
        return -1;
    }

    memset(s_allowed_pins, 0, sizeof(s_allowed_pins));
    s_allowed_count = 0;
    axk_register_default_pins();

    s_initialized = true;
    AXK_LOG_INFO("[gpio_policy] 安全策略初始化完成, %d个默认引脚在白名单\r\n", s_allowed_count);
    return 0;
}

int axk_gpio_policy_check(uint8_t pin, const char *action)
{
    size_t i;
    bool pin_ok = false;
    bool action_ok = false;

    if (!s_initialized) return -1;
    if (!action) return -1;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;

    /* 检查引脚是否在白名单 */
    for (i = 0; i < (size_t)s_allowed_count; i++) {
        if (s_allowed_pins[i] == pin) {
            pin_ok = true;
            break;
        }
    }

    if (!pin_ok) {
        AXK_LOG_WARN("[gpio_policy] 引脚 %d 不在白名单中\r\n", pin);
        xSemaphoreGive(s_mutex);
        return -1;
    }

    xSemaphoreGive(s_mutex);

    /* 检查操作是否允许 */
    for (i = 0; i < ACTION_COUNT; i++) {
        if (strcmp(ALLOWED_ACTIONS[i], action) == 0) {
            action_ok = true;
            break;
        }
    }

    if (!action_ok) {
        AXK_LOG_WARN("[gpio_policy] 操作 '%s' 不允许\r\n", action);
        return -1;
    }

    return 0;
}

int axk_gpio_policy_allow(uint8_t pin)
{
    int i;

    if (!s_initialized) return -1;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;

    /* 检查是否已在白名单 */
    for (i = 0; i < s_allowed_count; i++) {
        if (s_allowed_pins[i] == pin) {
            AXK_LOG_INFO("[gpio_policy] 引脚 %d 已在白名单\r\n", pin);
            xSemaphoreGive(s_mutex);
            return 0;
        }
    }

    /* 添加 */
    if (s_allowed_count >= AXK_GPIO_POLICY_MAX_PINS) {
        AXK_LOG_ERROR("[gpio_policy] 白名单已满\r\n");
        xSemaphoreGive(s_mutex);
        return -1;
    }

    s_allowed_pins[s_allowed_count++] = pin;
    xSemaphoreGive(s_mutex);
    AXK_LOG_INFO("[gpio_policy] 引脚 %d 已加入白名单\r\n", pin);
    return 0;
}

int axk_gpio_policy_deny(uint8_t pin)
{
    int i;

    if (!s_initialized) return -1;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;

    for (i = 0; i < s_allowed_count; i++) {
        if (s_allowed_pins[i] == pin) {
            /* 将最后一个元素移到当前位置 */
            s_allowed_count--;
            s_allowed_pins[i] = s_allowed_pins[s_allowed_count];
            AXK_LOG_INFO("[gpio_policy] 引脚 %d 已移出白名单\r\n", pin);
            xSemaphoreGive(s_mutex);
            return 0;
        }
    }

    AXK_LOG_WARN("[gpio_policy] 引脚 %d 不在白名单中\r\n", pin);
    xSemaphoreGive(s_mutex);
    return -1;
}

bool axk_gpio_policy_is_allowed(uint8_t pin)
{
    int i;
    bool allowed = false;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return false;

    for (i = 0; i < s_allowed_count; i++) {
        if (s_allowed_pins[i] == pin) {
            allowed = true;
            break;
        }
    }
    xSemaphoreGive(s_mutex);
    return allowed;
}

int axk_gpio_policy_list(char *buf, size_t size)
{
    size_t pos = 0;
    int i;

    if (!buf || size == 0) return 0;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return 0;

    pos += snprintf(buf + pos, size - pos, "GPIO白名单 (%d个):\r\n", s_allowed_count);

    for (i = 0; i < s_allowed_count; i++) {
        const char *name = "";
        switch (s_allowed_pins[i]) {
            case 10: name = " (KEY_0)"; break;
            case 11: name = " (KEY_1)"; break;
            case 12: name = " (LED_R)"; break;
            case 14: name = " (LED_G)"; break;
            case 15: name = " (LED_B)"; break;
        }
        pos += snprintf(buf + pos, size - pos, "  GPIO%d%s\r\n", s_allowed_pins[i], name);
        if (pos >= size - 1) break;
    }

    xSemaphoreGive(s_mutex);
    return (int)pos;
}

/* ── CLI 命令 ─────────────────────────────────────── */

/** gpio_allow <pin> - 将指定引脚加入白名单 */
static int cmd_gpio_allow(int argc, char **argv)
{
    int pin;
    if (argc < 2) {
        printf("Usage: gpio_allow <pin>\r\n");
        return 0;
    }
    pin = atoi(argv[1]);
    if (pin < 0 || pin > 34) {
        printf("Error: pin must be 0-34\r\n");
        return 0;
    }
    if (axk_gpio_policy_allow((uint8_t)pin) == 0) {
        printf("GPIO%d 已加入白名单\r\n", pin);
    } else {
        printf("Error: 无法添加 GPIO%d\r\n", pin);
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio_allow, gpio_allow, gpio_allow <pin> - add pin to whitelist);

/** gpio_deny <pin> - 从白名单移除指定引脚 */
static int cmd_gpio_deny(int argc, char **argv)
{
    int pin;
    if (argc < 2) {
        printf("Usage: gpio_deny <pin>\r\n");
        return 0;
    }
    pin = atoi(argv[1]);
    if (pin < 0 || pin > 34) {
        printf("Error: pin must be 0-34\r\n");
        return 0;
    }
    if (axk_gpio_policy_deny((uint8_t)pin) == 0) {
        printf("GPIO%d 已移出白名单\r\n", pin);
    } else {
        printf("Error: 无法移除 GPIO%d (不在白名单中)\r\n", pin);
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio_deny, gpio_deny, gpio_deny <pin> - remove pin from whitelist);

/** gpio_allow_list - 列出当前白名单中的引脚 */
static int cmd_gpio_allow_list(int argc, char **argv)
{
    char buf[512];
    (void)argc;
    (void)argv;
    axk_gpio_policy_list(buf, sizeof(buf));
    printf("%s", buf);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio_allow_list, gpio_allow_list, gpio_allow_list - list whitelisted pins);
