/**
 * @file axk_tool_web_search.h
 * @brief tool_web_search module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_WEB_SEARCH_H
#define __AXK_TOOL_WEB_SEARCH_H

#include "axk_platform.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化网络搜索工具
 * @return 0成功，负数错误码
 */
int axk_tool_web_search_init(void);
/**
 * @brief 执行网络搜索
 * @param[in] input_json 输入JSON（含query字段）
 * @param[out] output 输出缓冲区
 * @param[in] output_size 缓冲区大小
 * @return 0成功，负数错误码
 */
int axk_tool_web_search_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 设置搜索引擎提供商
 * @param[in] provider 提供商名称（如"duckduckgo"）
 * @return 0成功，负数错误码
 */
int axk_mimiclaw_web_search_set_provider(const char *provider);
/**
 * @brief 设置搜索API密钥
 * @param[in] key API密钥字符串
 * @return 0成功，负数错误码
 */
int axk_tool_web_search_set_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_WEB_SEARCH_H */
