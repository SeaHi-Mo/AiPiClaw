/**
 * @file axk_tool_gpio.h
 * @brief tool_gpio module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_GPIO_H
#define __AXK_TOOL_GPIO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化GPIO工具模块
 * @return 0成功，负数错误码
 */
int axk_tool_gpio_init(void);
/**
 * @brief 设置GPIO引脚输出电平
 * @param[in] input_json 输入JSON（含pin和level字段）
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0成功，负数错误码
 */
int axk_tool_gpio_write_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 读取GPIO引脚输入电平
 * @param[in] input_json 输入JSON（含pin字段）
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0成功，负数错误码
 */
int axk_tool_gpio_read_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 读取所有GPIO引脚状态
 * @param[in] input_json 输入JSON（空对象）
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0成功，负数错误码
 */
int axk_tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GPIO_H */
