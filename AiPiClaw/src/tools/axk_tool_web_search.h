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

int axk_tool_web_search_init(void);
int axk_tool_web_search_execute(const char *input_json, char *output, size_t output_size);
int axk_mimiclaw_web_search_set_provider(const char *provider);
int axk_tool_web_search_set_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_WEB_SEARCH_H */
