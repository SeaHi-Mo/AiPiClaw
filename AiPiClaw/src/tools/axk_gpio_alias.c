/**
 * @file axk_gpio_alias.c
 * @brief GPIO 别名注册表实现
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_gpio_alias.h"
#include "axk_platform.h"
#include <stdio.h>
#include <string.h>

/* 默认已注册别名的静态数组 */
static axk_gpio_alias_t s_aliases[AXK_GPIO_ALIAS_MAX];
static int s_alias_count = 0;
static bool s_initialized = false;

/**
 * @brief 注册默认板载别名项
 */
static void axk_register_default_aliases(void)
{
    /* AiPi-Eyes-DU 板载LED: 高电平=亮 (1=ON, 0=OFF) */
    const axk_gpio_alias_t defaults[] = {
        /* 红色LED - GPIO12 */
        { .name = "red_led",   .description = "红灯(GPIO12,高电平)",    .pin = 12, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "红灯",       .description = "红灯(GPIO12,高电平)",    .pin = 12, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "led_r",     .description = "红色LED(GPIO12)",        .pin = 12, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },

        /* 绿色LED - GPIO14 */
        { .name = "green_led", .description = "绿灯(GPIO14,高电平)",    .pin = 14, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "绿灯",       .description = "绿灯(GPIO14,高电平)",    .pin = 14, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "led_g",     .description = "绿色LED(GPIO14)",        .pin = 14, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },

        /* 蓝色LED - GPIO15 */
        { .name = "blue_led",  .description = "蓝灯(GPIO15,高电平)",   .pin = 15, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "蓝灯",       .description = "蓝灯(GPIO15,高电平)",   .pin = 15, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "led_b",     .description = "蓝色LED(GPIO15)",        .pin = 15, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_WRITE | AXK_GPIO_ALIAS_FLAG_READ },

        /* 按键 - 只读 */
        { .name = "key_0",     .description = "KEY_0按键(GPIO10,只读)", .pin = 10, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_READ },
        { .name = "key_1",     .description = "KEY_1按键(GPIO11,只读)", .pin = 11, .active_level = 1, .flags = AXK_GPIO_ALIAS_FLAG_READ },
    };

    size_t i;
    for (i = 0; i < sizeof(defaults) / sizeof(defaults[0]); i++) {
        axk_gpio_alias_register(&defaults[i]);
    }
}

int axk_gpio_alias_init(void)
{
    if (s_initialized) return 0;

    memset(s_aliases, 0, sizeof(s_aliases));
    s_alias_count = 0;

    axk_register_default_aliases();

    s_initialized = true;
    AXK_LOG_INFO("[gpio_alias] 别名注册表初始化完成, 共%d个别名\r\n", s_alias_count);
    return 0;
}

int axk_gpio_alias_register(const axk_gpio_alias_t *alias)
{
    if (!alias || !alias->name[0]) {
        return -1;
    }
    if (s_alias_count >= AXK_GPIO_ALIAS_MAX) {
        AXK_LOG_ERROR("[gpio_alias] 别名注册表已满\r\n");
        return -1;
    }

    s_aliases[s_alias_count] = *alias;
    s_aliases[s_alias_count].name[AXK_GPIO_ALIAS_NAME_MAX - 1] = '\0';
    s_aliases[s_alias_count].description[AXK_GPIO_ALIAS_DESC_MAX - 1] = '\0';
    s_alias_count++;

    AXK_LOG_DEBUG("[gpio_alias] 注册别名 '%s' -> GPIO%d\r\n", alias->name, alias->pin);
    return 0;
}

int axk_gpio_alias_resolve(const char *name, axk_gpio_alias_t *out)
{
    int i;

    if (!name || !out || !s_initialized) {
        return -1;
    }

    for (i = 0; i < s_alias_count; i++) {
        if (strcmp(s_aliases[i].name, name) == 0) {
            *out = s_aliases[i];
            return 0;
        }
    }

    return -1; /* 未找到 */
}

int axk_gpio_alias_get_count(void)
{
    return s_alias_count;
}

int axk_gpio_alias_list(char *buf, size_t size)
{
    int i;
    size_t pos = 0;

    if (!buf || size == 0) return 0;

    pos += snprintf(buf + pos, size - pos, "GPIO别名列表 (%d个):\r\n", s_alias_count);

    for (i = 0; i < s_alias_count; i++) {
        const char *rw = "RW";
        if (!(s_aliases[i].flags & AXK_GPIO_ALIAS_FLAG_WRITE)) {
            rw = "R-";
        }
        pos += snprintf(buf + pos, size - pos,
            "  %-16s → GPIO%-2d [%s] %s\r\n",
            s_aliases[i].name,
            s_aliases[i].pin,
            rw,
            s_aliases[i].description);

        if (pos >= size - 1) break;
    }

    return (int)pos;
}
