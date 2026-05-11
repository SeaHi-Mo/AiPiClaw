/**
 * @file axk_gpio_alias.c
 * @brief GPIO别名注册表实现
 * @version 1.0
 * @date 2026-05-11
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 管理逻辑名称到物理引脚的映射，支持中文别名
 *       LED 高电平有效 (active_high=1, 1=亮)，非共阳
 */

#include "axk_gpio_alias.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>

#define AXK_ALIAS_MAX 20  /**< 最大别名条目数 */

static axk_gpio_alias_t s_aliases[AXK_ALIAS_MAX];
static int s_alias_count = 0;
static bool s_initialized = false;

int axk_gpio_alias_init(void)
{
    if (s_initialized) return 0;

    s_alias_count = 0;
    memset(s_aliases, 0, sizeof(s_aliases));

    /* 板载 RGB LED — 高电平有效 (1=ON, 0=OFF) */
    axk_gpio_alias_t led_red = {
        .name = "red_led", .description = "红灯(GPIO12, 高电平有效)",
        .pin = 12, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_red);

    axk_gpio_alias_t led_red_cn = {
        .name = "红灯", .description = "红灯(GPIO12, 高电平有效)",
        .pin = 12, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_red_cn);

    axk_gpio_alias_t led_r = {
        .name = "led_r", .description = "红灯缩写(GPIO12)",
        .pin = 12, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_r);

    axk_gpio_alias_t led_green = {
        .name = "green_led", .description = "绿灯(GPIO14, 高电平有效)",
        .pin = 14, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_green);

    axk_gpio_alias_t led_green_cn = {
        .name = "绿灯", .description = "绿灯(GPIO14, 高电平有效)",
        .pin = 14, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_green_cn);

    axk_gpio_alias_t led_g = {
        .name = "led_g", .description = "绿灯缩写(GPIO14)",
        .pin = 14, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_g);

    axk_gpio_alias_t led_blue = {
        .name = "blue_led", .description = "蓝灯(GPIO15, 高电平有效)",
        .pin = 15, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_blue);

    axk_gpio_alias_t led_blue_cn = {
        .name = "蓝灯", .description = "蓝灯(GPIO15, 高电平有效)",
        .pin = 15, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_blue_cn);

    axk_gpio_alias_t led_b = {
        .name = "led_b", .description = "蓝灯缩写(GPIO15)",
        .pin = 15, .active_level = 1, .flags = AXK_ALIAS_FLAG_WRITE | AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&led_b);

    /* 板载按键 — 只读 */
    axk_gpio_alias_t key0 = {
        .name = "key_0", .description = "按键KEY0(GPIO10, 高电平有效)",
        .pin = 10, .active_level = 1, .flags = AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&key0);

    axk_gpio_alias_t btn0 = {
        .name = "button_0", .description = "按键KEY0(GPIO10)",
        .pin = 10, .active_level = 1, .flags = AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&btn0);

    axk_gpio_alias_t key1 = {
        .name = "key_1", .description = "按键KEY1(GPIO11, 高电平有效)",
        .pin = 11, .active_level = 1, .flags = AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&key1);

    axk_gpio_alias_t btn1 = {
        .name = "button_1", .description = "按键KEY1(GPIO11)",
        .pin = 11, .active_level = 1, .flags = AXK_ALIAS_FLAG_READ,
    };
    axk_gpio_alias_register(&btn1);

    s_initialized = true;
    AXK_LOG_INFO("[axk_gpio_alias] 别名表初始化完成，%d 条目\r\n", s_alias_count);
    return 0;
}

int axk_gpio_alias_register(const axk_gpio_alias_t *alias)
{
    if (!alias || !alias->name || s_alias_count >= AXK_ALIAS_MAX) {
        return -1;
    }

    /* 去重：同名覆盖 */
    for (int i = 0; i < s_alias_count; i++) {
        if (strcmp(s_aliases[i].name, alias->name) == 0) {
            s_aliases[i] = *alias;
            return 0;
        }
    }

    s_aliases[s_alias_count++] = *alias;
    return 0;
}

int axk_gpio_alias_resolve(const char *name, axk_gpio_alias_t *out)
{
    if (!name || !out) return -1;

    for (int i = 0; i < s_alias_count; i++) {
        if (strcmp(s_aliases[i].name, name) == 0) {
            *out = s_aliases[i];
            return 0;
        }
    }
    return -1;
}

int axk_gpio_alias_list(char *buf, size_t size)
{
    int pos = 0;
    pos += snprintf(buf + pos, size - pos, "GPIO 别名列表 (%d):\r\n", s_alias_count);
    for (int i = 0; i < s_alias_count; i++) {
        const char *rw = (s_aliases[i].flags & AXK_ALIAS_FLAG_WRITE) ? "W" : "-";
        const char *rd = (s_aliases[i].flags & AXK_ALIAS_FLAG_READ) ? "R" : "-";
        pos += snprintf(buf + pos, size - pos,
            "  %-16s -> GPIO%-2d %s%s  %s\r\n",
            s_aliases[i].name, s_aliases[i].pin, rd, rw,
            s_aliases[i].description);
    }
    return pos;
}

int axk_gpio_alias_count(void)
{
    return s_alias_count;
}
