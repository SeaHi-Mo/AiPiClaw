/**
 * @file axk_tool_gpio_named.c
 * @brief MCP GPIO 别名控制工具 - 通过别名控制GPIO
 * @version 1.0
 * @date 2026-05-11
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 提供 gpio_write_named / gpio_read_named 两个工具
 *       支持中文别名如"绿灯"，自动将 on/off 映射为高/低电平
 *       LED 高电平有效 (1=ON, 0=OFF)，非共阳
 */

#include "axk_gpio_alias.h"
#include "axk_hal_gpio.h"
#include "axk_gpio_policy.h"
#include "axk_platform.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief 通过别名写入GPIO状态
 * @param[in] input_json 输入JSON: {"pin_name":"green_led", "state":"on"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 输出缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_write_named_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    cJSON *item;
    axk_gpio_alias_t alias;
    const char *pin_name;
    const char *state_str;
    int level;

    if (!input_json || !output || output_size == 0) {
        if (output) snprintf(output, output_size, "Error: invalid arguments");
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    /* 解析 pin_name */
    item = cJSON_GetObjectItem(root, "pin_name");
    if (!cJSON_IsString(item)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing or invalid 'pin_name' field");
        return -1;
    }
    pin_name = item->valuestring;

    /* 解析 state */
    item = cJSON_GetObjectItem(root, "state");
    if (!cJSON_IsString(item)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing or invalid 'state' field (expect 'on'/'off'/'toggle')");
        return -1;
    }
    state_str = item->valuestring;

    /* 别名解析 */
    if (axk_gpio_alias_resolve(pin_name, &alias) != 0) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: unknown pin name '%s'. Use gpio_alias to list available names.", pin_name);
        return -1;
    }

    /* 权限检查：是否允许写 */
    if (!(alias.flags & AXK_ALIAS_FLAG_WRITE)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: pin '%s' (GPIO%d) is read-only", pin_name, alias.pin);
        return -1;
    }

    /* 策略检查 */
    if (axk_gpio_policy_check(alias.pin, "set_level") != 0) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: GPIO%d not allowed by policy", alias.pin);
        return -1;
    }

    /* 处理 state: on/off/toggle */
    if (strcmp(state_str, "toggle") == 0) {
        int current = axk_hal_gpio_get_level(alias.pin);
        level = (current > 0) ? 0 : 1;
    } else if (strcmp(state_str, "on") == 0) {
        level = alias.active_level;  /* 高电平有效 → level=1 */
    } else if (strcmp(state_str, "off") == 0) {
        level = (alias.active_level == 1) ? 0 : 1;
    } else {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: invalid state '%s' (use 'on', 'off', or 'toggle')", state_str);
        return -1;
    }

    /* 配置为输出 */
    axk_gpio_cfg_t cfg = {0};
    cfg.pin = alias.pin;
    cfg.mode = AXK_GPIO_MODE_OUT;
    cfg.pull = AXK_GPIO_PULL_NONE;
    cfg.drive = AXK_GPIO_DRIVE_STRONG;
    axk_hal_gpio_config(alias.pin, &cfg);

    /* 执行写操作 */
    axk_hal_gpio_set_level(alias.pin, (uint32_t)level);

    const char *state_desc = (level == alias.active_level) ? "亮" : "灭";
    snprintf(output, output_size, "%s 已%s (GPIO%d=%d)", alias.description, state_desc, alias.pin, level);
    AXK_LOG_INFO("[gpio_named] %s → GPIO%d=%d\r\n", pin_name, alias.pin, level);

    cJSON_Delete(root);
    return 0;
}

/**
 * @brief 通过别名读取GPIO状态
 * @param[in] input_json 输入JSON: {"pin_name":"green_led"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 输出缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_read_named_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    cJSON *item;
    axk_gpio_alias_t alias;
    const char *pin_name;
    int level;

    if (!input_json || !output || output_size == 0) {
        if (output) snprintf(output, output_size, "Error: invalid arguments");
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    item = cJSON_GetObjectItem(root, "pin_name");
    if (!cJSON_IsString(item)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: missing or invalid 'pin_name' field");
        return -1;
    }
    pin_name = item->valuestring;

    /* 别名解析 */
    if (axk_gpio_alias_resolve(pin_name, &alias) != 0) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: unknown pin name '%s'", pin_name);
        return -1;
    }

    /* 权限检查：是否允许读 */
    if (!(alias.flags & AXK_ALIAS_FLAG_READ)) {
        cJSON_Delete(root);
        snprintf(output, output_size, "Error: pin '%s' (GPIO%d) is write-only", pin_name, alias.pin);
        return -1;
    }

    /* 配置为输入读取 */
    axk_gpio_cfg_t cfg = {0};
    cfg.pin = alias.pin;
    cfg.mode = AXK_GPIO_MODE_IN;
    cfg.pull = AXK_GPIO_PULL_NONE;
    axk_hal_gpio_config(alias.pin, &cfg);

    level = axk_hal_gpio_get_level(alias.pin);

    const char *state_desc = (level == alias.active_level) ? "亮(on)" : "灭(off)";
    snprintf(output, output_size, "%s 当前状态: %s (GPIO%d=%d)", alias.description, state_desc, alias.pin, level);
    AXK_LOG_INFO("[gpio_named] %s read GPIO%d=%d\r\n", pin_name, alias.pin, level);

    cJSON_Delete(root);
    return 0;
}
