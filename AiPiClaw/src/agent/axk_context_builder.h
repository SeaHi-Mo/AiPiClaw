/**
 * @file axk_context_builder.h
 * @brief context_builder module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_CONTEXT_BUILDER_H
#define __AXK_CONTEXT_BUILDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TODO: 描述axk_context_builder_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_context_builder_init(void);
const char *axk_context_builder_get_system_prompt(void);
char *axk_context_builder_build_request(const char *user_message,
                                         const char *tools_json,
                                         const char *session_context);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_CONTEXT_BUILDER_H */
