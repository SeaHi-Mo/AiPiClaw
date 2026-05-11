/**
 * @file axk_tool_gpio_named.c
 * @brief GPIO 命名引脚工具 - 通过别名控制/读取GPIO
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 依赖 axk_gpio_alias 别名注册表，LLM 通过语义名操作 GPIO
 */

#include "axk_tool_gpio.h"
#include "axk_gpio_alias.h"
#include "axk_gpio_policy.h"
#include "axk_hal_gpio.h"
#include "cJSON.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/**
 * @brief 将 on/off/toggle 字符串转换为电平值和翻转标志
 * @param[in] state 状态字符串 ("on" / "off" / "toggle")
 * @param[out] level 输出电平值 (0 或 1)
 * @param[out] do_toggle 是否翻转
 * @return 0 成功，-1 未知状态
 */
static int parse_state(const char *state, int *level, int *do_toggle)
{
    if (!state) return -1;

    if (strcmp(state, "on") == 0) {
        *level = 1;
        *do_toggle = 0;
        return 0;
    }
    if (strcmp(state, "off") == 0) {
        *level = 0;
        *do_toggle = 0;
        return 0;
    }
    if (strcmp(state, "toggle") == 0) {
        *level = 0;
        *do_toggle = 1;
        return 0;
    }
    return -1;
}

/**
 * @brief 获取引脚的当前开关状态描述
 * @param[in] alias 别名条目
 * @param[out] buf 输出缓冲区
 * @param[in] size 缓冲区大小
 * @return 写入的字符数
 */
static int get_state_str(const axk_gpio_alias_t *alias, char *buf, size_t size)
{
    int raw = axk_hal_gpio_get_level(alias->pin);
    int on = (raw == alias->active_level) ? 1 : 0;
    return snprintf(buf, size, "%s", on ? "on" : "off");
}

/**
 * @brief 执行带别名的 GPIO 写入操作
 * @param[in] input_json JSON 输入：{"pin_name":"green_led","state":"on|off|toggle"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_write_named_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    cJSON *item;
    axk_gpio_alias_t alias;
    int level, do_toggle;
    const char *pin_name = NULL;
    const char *state = NULL;
    static bool policy_inited = false;

    if (!policy_inited) {
        axk_gpio_policy_init();
        policy_inited = true;
    }

    if (!input_json || !output || output_size == 0) {
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
        snprintf(output, output_size, "Error: missing or invalid 'pin_name'");
        cJSON_Delete(root);
        return -1;
    }
    pin_name = item->valuestring;

    /* 解析 state */
    item = cJSON_GetObjectItem(root, "state");
    if (!cJSON_IsString(item)) {
        snprintf(output, output_size, "Error: missing or invalid 'state'");
        cJSON_Delete(root);
        return -1;
    }
    state = item->valuestring;

    cJSON_Delete(root);

    printf("[named_dbg] write pin_name='%s' state='%s'\r\n", pin_name ? pin_name : "NULL", state ? state : "NULL");
    fflush(stdout);

    /* 解析别名 */
    if (axk_gpio_alias_resolve(pin_name, &alias) != 0) {
        printf("[named_dbg] resolve FAIL: '%s'\r\n", pin_name);
        fflush(stdout);
        snprintf(output, output_size, "Error: unknown pin name '%s'. Use gpio_alias to list available aliases.", pin_name);
        return -1;
    }

    printf("[named_dbg] resolve OK: pin=%d active=%d flags=0x%x desc='%s'\r\n",
           alias.pin, alias.active_level, alias.flags, alias.description);
    fflush(stdout);

    /* 检查写权限 */
    if (!(alias.flags & AXK_GPIO_ALIAS_FLAG_WRITE)) {
        printf("[named_dbg] WRITE DENIED: no write flag\r\n");
        fflush(stdout);
        return -1;
    }

    /* 检查安全策略 */
    if (axk_gpio_policy_check(alias.pin, "set_level") != 0) {
        printf("[named_dbg] POLICY DENIED: pin=%d action=set_level\r\n", alias.pin);
        fflush(stdout);
        snprintf(output, output_size, "Error: GPIO%d (%s) not allowed by security policy", alias.pin, alias.description);
        return -1;
    }

    /* 解析状态 */
    if (parse_state(state, &level, &do_toggle) != 0) {
        printf("[named_dbg] parse_state FAIL: '%s'\r\n", state);
        fflush(stdout);
        snprintf(output, output_size, "Error: unknown state '%s'. Use on/off/toggle.", state);
        return -1;
    }

    printf("[named_dbg] parsed: state='%s' -> level_init=%d do_toggle=%d\r\n", state, level, do_toggle);
    fflush(stdout);

    /* 执行操作 */
    if (do_toggle) {
        /* 翻转: 先配为输入读当前值，再配为输出写翻转值 */
        axk_gpio_cfg_t in_cfg = { .mode = AXK_GPIO_MODE_IN, .pull = AXK_GPIO_PULL_NONE, .drive = AXK_GPIO_DRIVE_WEAK };
        axk_hal_gpio_config(alias.pin, &in_cfg);
        int current = axk_hal_gpio_get_level(alias.pin);
        printf("[named_dbg] toggle: current_raw=%d active=%d\r\n", current, alias.active_level);
        fflush(stdout);
        level = (current == alias.active_level) ? 0 : alias.active_level;
        /* 再配置为输出 */
        axk_gpio_cfg_t out_cfg = { .mode = AXK_GPIO_MODE_OUT, .pull = AXK_GPIO_PULL_NONE, .drive = AXK_GPIO_DRIVE_STRONG };
        axk_hal_gpio_config(alias.pin, &out_cfg);
    } else {
        /* 将 on/off 语义映射为 pin 的物理电平，配置为输出 */
        level = level ? alias.active_level : (!alias.active_level);
        axk_gpio_cfg_t out_cfg = { .mode = AXK_GPIO_MODE_OUT, .pull = AXK_GPIO_PULL_NONE, .drive = AXK_GPIO_DRIVE_STRONG };
        axk_hal_gpio_config(alias.pin, &out_cfg);
    }

    printf("[named_dbg] calling set_level(pin=%d, level=%d)\r\n", alias.pin, level);
    fflush(stdout);

    axk_hal_gpio_set_level(alias.pin, (uint32_t)level);

    /* verify */
    {
        int check = axk_hal_gpio_get_level(alias.pin);
        printf("[named_dbg] after set_level: get_level=%d (expected %d)\r\n", check, level);
        fflush(stdout);
    }

    {
        char state_buf[8];
        get_state_str(&alias, state_buf, sizeof(state_buf));
        snprintf(output, output_size, "%s 已%s (%s)", alias.description,
                 level == alias.active_level ? "点亮" : "熄灭", state_buf);
    }

    AXK_LOG_INFO("[tool_gpio_named] %s -> GPIO%d=%d\r\n", pin_name, alias.pin, level);
    return 0;
}

/**
 * @brief 执行带别名的 GPIO 读取操作
 * @param[in] input_json JSON 输入：{"pin_name":"green_led"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_read_named_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    cJSON *item;
    axk_gpio_alias_t alias;
    const char *pin_name = NULL;

    if (!input_json || !output || output_size == 0) {
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    item = cJSON_GetObjectItem(root, "pin_name");
    if (!cJSON_IsString(item)) {
        snprintf(output, output_size, "Error: missing or invalid 'pin_name'");
        cJSON_Delete(root);
        return -1;
    }
    pin_name = item->valuestring;
    cJSON_Delete(root);

    /* 解析别名 */
    if (axk_gpio_alias_resolve(pin_name, &alias) != 0) {
        snprintf(output, output_size, "Error: unknown pin name '%s'", pin_name);
        return -1;
    }

    /* 检查读权限 */
    if (!(alias.flags & AXK_GPIO_ALIAS_FLAG_READ)) {
        snprintf(output, output_size, "Error: '%s' does not support read", pin_name);
        return -1;
    }

    /* 配置为输入，然后读取状态 */
    {
        axk_gpio_cfg_t in_cfg = { .mode = AXK_GPIO_MODE_IN, .pull = AXK_GPIO_PULL_NONE, .drive = AXK_GPIO_DRIVE_WEAK };
        axk_hal_gpio_config(alias.pin, &in_cfg);
    }
    {
        char state_buf[8];
        get_state_str(&alias, state_buf, sizeof(state_buf));
        snprintf(output, output_size, "%s 当前状态: %s (%s)", alias.description,
                 state_buf, state_buf);
    }

    return 0;
}
