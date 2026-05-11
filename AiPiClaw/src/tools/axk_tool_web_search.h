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

/* @brief TODO: 描述axk_tool_web_search_init的功能 @return 0成功, -1失败 */
int axk_tool_web_search_init(void);
/* @brief TODO: 描述axk_tool_web_search_execute的功能 @param input_json TODO: 描述input_json @param output TODO: 描述output @param output_size TODO: 描述output_size @return 0成功, -1失败 */
int axk_tool_web_search_execute(const char *input_json, char *output, size_t output_size);
/* @brief TODO: 描述axk_mimiclaw_web_search_set_provider的功能 @param provider TODO: 描述provider @return 0成功, -1失败 */
int axk_mimiclaw_web_search_set_provider(const char *provider);
/* @brief TODO: 描述axk_tool_web_search_set_key的功能 @param key TODO: 描述key @return 0成功, -1失败 */
int axk_tool_web_search_set_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_WEB_SEARCH_H */
