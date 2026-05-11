/**
 * @file axk_tool_gpio_named.h
 * @brief GPIO 命名引脚工具 - 头文件
 * @version 1.0
 * @date 2026-05-11
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_GPIO_NAMED_H
#define __AXK_TOOL_GPIO_NAMED_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 通过别名执行GPIO写入操作
 * @param[in] input_json JSON: {"pin_name":"green_led","state":"on|off|toggle"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_write_named_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief 通过别名执行GPIO读取操作
 * @param[in] input_json JSON: {"pin_name":"green_led"}
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0 成功，-1 失败
 */
int axk_tool_gpio_read_named_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GPIO_NAMED_H */
