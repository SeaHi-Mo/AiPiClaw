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
 * @brief 初始化网络搜索工具，注册搜索API执行接口并加载持久化配置
 *
 * @return 0成功, -1失败
 */
int axk_tool_web_search_init(void);
/**
 * @brief 执行网络搜索查询：解析JSON输入，调用Bocha/Brave搜索API并返回格式化结果
 *
 * @param input_json JSON输入字符串，需包含query（搜索关键词），可选freshness/count/summary
 * @param output 输出缓冲区，用于存放搜索结果JSON字符串
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_web_search_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 设置网络搜索服务提供商并持久化到KV存储
 *
 * @param provider 提供商名称字符串（"bocha"或"brave"）
 * @return 0成功, -1失败
 */
int axk_mimiclaw_web_search_set_provider(const char *provider);
/**
 * @brief 设置搜索API密钥并持久化到KV存储
 *
 * @param key API密钥字符串
 * @return 0成功, -1失败
 */
int axk_tool_web_search_set_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_WEB_SEARCH_H */
