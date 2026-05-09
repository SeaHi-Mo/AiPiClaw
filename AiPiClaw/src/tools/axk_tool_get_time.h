/**
 * @file axk_tool_get_time.h
 * @brief tool_get_time module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_GET_TIME_H
#define __AXK_TOOL_GET_TIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_tool_get_time_init(void);
int axk_tool_get_time_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GET_TIME_H */
