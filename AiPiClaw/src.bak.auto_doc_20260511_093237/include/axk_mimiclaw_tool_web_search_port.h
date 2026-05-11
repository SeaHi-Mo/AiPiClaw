/**
 * @file axk_mimiclaw_tool_web_search_port.h
 * @brief 网络搜索tool平台接口 - 安信可科技 BL618 port
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef AXK_MIMICLAW_TOOL_WEB_SEARCH_PORT_H
#define AXK_MIMICLAW_TOOL_WEB_SEARCH_PORT_H

#include "axk_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *axk_mimiclaw_web_search_get_provider(void);
int axk_mimiclaw_web_search_set_provider(const char *provider);

#ifdef __cplusplus
}
#endif

#endif
