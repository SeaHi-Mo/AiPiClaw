/**
 * @file axk_tool_board_info.h
 * @brief board_info tool - 返回板卡和系统信息
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_BOARD_INFO_H
#define __AXK_TOOL_BOARD_INFO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 注册 board_info 工具到工具注册表
 */
void axk_tool_board_info_register(void);

/**
 * @brief 执行 board_info 工具调用
 * @param[in]  input_json  JSON 输入（可为空或 {}）
 * @param[out] output      输出 JSON 缓冲区
 * @param[in]  output_size 输出缓冲区大小
 * @return 0 成功, -1 失败
 */
int axk_tool_board_info_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_BOARD_INFO_H */
