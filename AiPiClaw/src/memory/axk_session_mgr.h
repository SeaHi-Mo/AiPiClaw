/**
 * @file axk_session_mgr.h
 * @brief session_mgr module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_SESSION_MGR_H
#define __AXK_SESSION_MGR_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_session_mgr_init(void);
int axk_session_update_context(const char *session_id, const char *context);
int axk_session_get_context(const char *session_id, char *buf, size_t buf_size);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_SESSION_MGR_H */
