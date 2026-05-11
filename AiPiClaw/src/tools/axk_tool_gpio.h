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
 * @brief 初始化GPIO控制工具模块
 * @return 0成功, -1失败
 */
int axk_tool_gpio_init(void);
/**
 * @brief 执行GPIO写操作，设置指定引脚电平
 * @param[in] input_json JSON: {"pin":N, "value":0|1}
 * @param[out] output 执行结果字符串
 * @param[in] output_size 输出缓冲区大小
 * @return 0成功, -1失败
 */
int axk_tool_gpio_write_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 执行GPIO读操作，读取指定引脚电平
 * @param[in] input_json JSON: {"pin":N}
 * @param[out] output 执行结果字符串
 * @param[in] output_size 输出缓冲区大小
 * @return 0成功, -1失败
 */
int axk_tool_gpio_read_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 读取所有合法GPIO引脚的当前电平
 * @param[in] input_json JSON: {} (预留)
 * @param[out] output 按行列出各GPIO状态
 * @param[in] output_size 输出缓冲区大小
 * @return 0成功, -1失败
 */
int axk_tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GPIO_H */
