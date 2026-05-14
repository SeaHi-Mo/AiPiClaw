/**
 * @file axk_tool_gpio_list.h
 * @brief gpio_list_aliases tool - 列出所有GPIO别名及其状态
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_GPIO_LIST_H
#define __AXK_TOOL_GPIO_LIST_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 gpio_list_aliases 工具到工具注册表
 */
void axk_tool_gpio_list_register(void);

/**
 * @brief 执行 gpio_list_aliases 工具调用
 * @param[in]  input_json  JSON 输入（可为空或 {}）
 * @param[out] output      输出 JSON 缓冲区
 * @param[in]  output_size 输出缓冲区大小
 * @return 0 成功, -1 失败
 */
int axk_tool_gpio_list_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GPIO_LIST_H */
