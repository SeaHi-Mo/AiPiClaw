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

/* @brief TODO: 描述axk_tool_gpio_init的功能 @return 0成功, -1失败 */
int axk_tool_gpio_init(void);
/* @brief TODO: 描述axk_tool_gpio_write_execute的功能 @param input_json TODO: 描述input_json @param output TODO: 描述output @param output_size TODO: 描述output_size @return 0成功, -1失败 */
int axk_tool_gpio_write_execute(const char *input_json, char *output, size_t output_size);
/* @brief TODO: 描述axk_tool_gpio_read_execute的功能 @param input_json TODO: 描述input_json @param output TODO: 描述output @param output_size TODO: 描述output_size @return 0成功, -1失败 */
int axk_tool_gpio_read_execute(const char *input_json, char *output, size_t output_size);
/* @brief TODO: 描述axk_tool_gpio_read_all_execute的功能 @param input_json TODO: 描述input_json @param output TODO: 描述output @param output_size TODO: 描述output_size @return 0成功, -1失败 */
int axk_tool_gpio_read_all_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GPIO_H */
