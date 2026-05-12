/**
 * @file axk_gpio_alias.c
 * @brief GPIO 别名注册表实现
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_gpio_alias.h"
#include "axk_platform.h"
#include "axk_hal_gpio.h"
#include "shell.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdio.h>
#include <string.h>

/* 默认已注册别名的静态数组 */
static axk_gpio_alias_t s_aliases[AXK_GPIO_ALIAS_MAX];
static int s_alias_count = 0;
static SemaphoreHandle_t s_mutex = NULL;
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

    s_mutex = xSemaphoreCreateMutex();
    if (!s_mutex) {
        AXK_LOG_ERROR("[gpio_alias] 创建互斥锁失败\r\n");
        return -1;
    }

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

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;

    if (s_alias_count >= AXK_GPIO_ALIAS_MAX) {
        AXK_LOG_ERROR("[gpio_alias] 别名注册表已满\r\n");
        xSemaphoreGive(s_mutex);
        return -1;
    }

    s_aliases[s_alias_count] = *alias;
    s_aliases[s_alias_count].name[AXK_GPIO_ALIAS_NAME_MAX - 1] = '\0';
    s_aliases[s_alias_count].description[AXK_GPIO_ALIAS_DESC_MAX - 1] = '\0';
    s_alias_count++;

    xSemaphoreGive(s_mutex);
    AXK_LOG_DEBUG("[gpio_alias] 注册别名 '%s' -> GPIO%d\r\n", alias->name, alias->pin);
    return 0;
}

int axk_gpio_alias_resolve(const char *name, axk_gpio_alias_t *out)
{
    int i;

    if (!name || !out || !s_initialized) {
        return -1;
    }

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return -1;

    for (i = 0; i < s_alias_count; i++) {
        if (strcmp(s_aliases[i].name, name) == 0) {
            *out = s_aliases[i];
            xSemaphoreGive(s_mutex);
            return 0;
        }
    }

    xSemaphoreGive(s_mutex);
    return -1; /* 未找到 */
}

int axk_gpio_alias_get_count(void)
{
    int count;
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return 0;
    count = s_alias_count;
    xSemaphoreGive(s_mutex);
    return count;
}

int axk_gpio_alias_list(char *buf, size_t size)
{
    int i;
    size_t pos = 0;

    if (!buf || size == 0) return 0;

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) != pdTRUE) return 0;

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

    xSemaphoreGive(s_mutex);
    return (int)pos;
}

/* ── CLI 命令 ─────────────────────────────────────── */

/** gpio_alias - 显示所有已注册的别名 */
static int cmd_gpio_alias(int argc, char **argv)
{
    char buf[1024];
    (void)argc;
    (void)argv;
    axk_gpio_alias_list(buf, sizeof(buf));
    printf("%s", buf);
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio_alias, gpio_alias, gpio_alias - list all GPIO aliases);

/** gpio_status - 显示所有别名引脚的当前状态 */
static int cmd_gpio_status(int argc, char **argv)
{
    int i;
    (void)argc;
    (void)argv;

    printf("GPIO状态:\r\n");
    for (i = 0; i < s_alias_count; i++) {
        int raw = axk_hal_gpio_get_level(s_aliases[i].pin);
        int on = (raw == s_aliases[i].active_level) ? 1 : 0;
        printf("  %-16s GPIO%-2d 状态=%s (%s)\r\n",
               s_aliases[i].name,
               s_aliases[i].pin,
               on ? "on" : "off",
               s_aliases[i].description);
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio_status, gpio_status, gpio_status - show GPIO alias pin states);
