/**
 * @file axk_tool_gpio.c
 * @brief GPIO\u63a7\u5236\u5de5\u5177 - BL618\u79fb\u690d\u7248
 * @version 1.0
 * @date 2026-04-24
 * @copyright Copyright (c) 2026 \u5b89\u4fe1\u53ef\u79d1\u6280\u6709\u9650\u516c\u53f8
 * @note \u63d0\u4f9bGPIO\u8bfb\u5199\u529f\u80fd\uff0c\u652f\u6301BL618\u82af\u7247\u7684GPIO\u5f15\u811a
 */

#include "axk_tool_gpio.h"
#include "axk_platform.h"
#include "axk_hal_gpio.h"
#include "cJSON.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char *TAG = "tool_gpio";

/* BL616/BL618 \u5f00\u53d1\u677f\u53ef\u7528GPIO\u5217\u8868 (\u6392\u9664\u7cfb\u7edf\u4fdd\u7559\u5f15\u811a) */
static const int s_valid_gpio_pins[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
    20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34
};
#define AXK_VALID_GPIO_COUNT (sizeof(s_valid_gpio_pins) / sizeof(s_valid_gpio_pins[0]))

/**
 * @brief \u68c0\u67e5GPIO\u5f15\u811a\u662f\u5426\u5408\u6cd5
 * @param[in] pin \u5f15\u811a\u53f7
 * @return \u5408\u6cd5\u8fd4\u56detrue
 */
static bool axk_gpio_pin_valid(int pin)
{
    size_t i;
    for (i = 0; i < AXK_VALID_GPIO_COUNT; i++) {
        if (s_valid_gpio_pins[i] == pin) {
            return true;
        }
    }
    return false;
}

/**
 * @brief \u89e3\u6790JSON\u8f93\u5165\uff0c\u63d0\u53d6pin\u548cvalue
 * @param[in] input_json \u8f93\u5165JSON\u5b57\u7b26\u4e32
 * @param[out] pin \u5f15\u811a\u53f7\u8f93\u51fa
 * @param[out] value \u7535\u5e73\u503c\u8f93\u51fa\uff08\u53ea\u5bf9write\u6709\u6548\uff09
 * @param[out] mode \u6a21\u5f0f\u8f93\u51fa\uff080=in, 1=out\uff09
 * @return \u6210\u529f\u8fd4\u56de0
 */
static int axk_gpio_parse_input(const char *input_json, int *pin, int *value, int *mode)
{
    cJSON *root;
    cJSON *item;

    if (!input_json || !pin) {
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        return -1;
    }

    item = cJSON_GetObjectItem(root, "pin");
    if (!cJSON_IsNumber(item)) {
        cJSON_Delete(root);
        return -1;
    }
    *pin = item->valueint;

    if (value) {
        item = cJSON_GetObjectItem(root, "value");
        *value = cJSON_IsNumber(item) ? item->valueint : 0;
    }

    if (mode) {
        item = cJSON_GetObjectItem(root, "mode");
        if (cJSON_IsString(item) && strcmp(item->valuestring, "output") == 0) {
            *mode = AXK_GPIO_MODE_OUT;
        } else {
            *mode = AXK_GPIO_MODE_IN;
        }
    }

    cJSON_Delete(root);

    if (!axk_gpio_pin_valid(*pin)) {
        AXK_LOG_WARN("warn", "GPIO%d \u4e0d\u5728\u53ef\u7528\u5217\u8868\u4e2d", *pin);
        return -1;
    }

    return 0;
}

/* @brief 初始化GPIO控制工具 @return 0成功, -1失败 */
int axk_tool_gpio_init(void)
{
    AXK_LOG_INFO("info", "GPIO\u5de5\u5177\u521d\u59cb\u5316\u5b8c\u6210\uff0c\u652f\u6301 %d \u4e2aGPIO\u5f15\u811a", (int)AXK_VALID_GPIO_COUNT);
    return 0;
}

/* @brief 执行GPIO写操作 @param[in] input_json 输入JSON（含pin和value字段） @param[out] output 输出缓冲区 @param[in] output_size 输出缓冲区大小 @return 0成功, -1失败 */
int axk_tool_gpio_write_execute(const char *input_json, char *output, size_t output_size)
{
    int pin;
    int value;
    int mode;
    axk_gpio_cfg_t cfg = {0};
    int ret;

    ret = axk_gpio_parse_input(input_json, &pin, &value, &mode);
    if (ret != 0) {
        snprintf(output, output_size, "Error: invalid input. Expected {\"pin\":N,\"value\":0|1}");
        return ret;
    }

    cfg.pin = (uint32_t)pin;
    cfg.mode = (uint32_t)mode;
    cfg.pull = AXK_GPIO_PULL_NONE;
    axk_hal_gpio_config(pin, &cfg);

    axk_hal_gpio_set_level((uint32_t)pin, (uint32_t)value);

    snprintf(output, output_size, "GPIO%d set to %d", pin, value);
    AXK_LOG_INFO("info", "GPIO%d set to %d", pin, value);
    return 0;
}

/* @brief 执行GPIO读操作 @param[in] input_json 输入JSON（含pin字段） @param[out] output 输出缓冲区 @param[in] output_size 输出缓冲区大小 @return 0成功, -1失败 */
int axk_tool_gpio_read_execute(const char *input_json, char *output, size_t output_size)
{
    int pin;
    int level;
    axk_gpio_cfg_t cfg = {0};
    int ret;

    ret = axk_gpio_parse_input(input_json, &pin, NULL, NULL);
    if (ret != 0) {
        snprintf(output, output_size, "Error: invalid input. Expected {\"pin\":N}");
        return ret;
    }

    cfg.pin = (uint32_t)pin;
    cfg.mode = AXK_GPIO_MODE_IN;
    cfg.pull = AXK_GPIO_PULL_NONE;
    axk_hal_gpio_config(pin, &cfg);

    level = axk_hal_gpio_get_level((uint32_t)pin);

    snprintf(output, output_size, "GPIO%d = %d", pin, level);
    AXK_LOG_INFO("info", "GPIO%d read = %d", pin, level);
    return 0;
}

/* @brief 读取所有GPIO引脚状态 @param[in] input_json 输入JSON（预留） @param[out] output 输出缓冲区 @param[in] output_size 输出缓冲区大小 @return 0成功, -1失败 */
int axk_tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size)
{
    size_t i;
    size_t pos = 0;
    (void)input_json;

    pos += snprintf(output + pos, output_size - pos, "GPIO states:\n");

    for (i = 0; i < AXK_VALID_GPIO_COUNT; i++) {
        int pin = s_valid_gpio_pins[i];
        int level = axk_hal_gpio_get_level((uint32_t)pin);
        int n = snprintf(output + pos, output_size - pos, "  GPIO%2d = %d\n", pin, level);
        if (n > 0) {
            pos += (size_t)n;
        }
        if (pos >= output_size - 1) {
            break;
        }
    }

    return 0;
}
