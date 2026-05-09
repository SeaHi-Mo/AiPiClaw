#pragma once

#include <stddef.h>

/**
 * Initialize web search tool.
 */
int axk_tool_web_search_init(void);

/**
 * Execute a web search.
 *
 * @param input_json   JSON string with "query" field
 * @param output       Output buffer for formatted search results
 * @param output_size  Size of output buffer
 * @return 0 on success
 */
int axk_tool_web_search_execute(const char *input_json, char *output, size_t output_size);

/**
 * Save Brave Search API key to NVS.
 */
int axk_tool_web_search_set_key(const char *api_key);

/**
 * Save Tavily API key to NVS.
 */
int axk_tool_web_search_set_tavily_key(const char *api_key);
