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

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 会话ID最大长度 (channel:chat_id, 如 "websocket:client1" 最大 ~113 字节) */
#define AXK_SESSION_ID_LEN    128

/** 会话上下文最大长度 (向后兼容, 新代码使用 MIMI_CONTEXT_BUF_SIZE) */
#define AXK_SESSION_CTX_LEN   512

/**
 * @brief TODO: 描述axk_session_mgr_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_session_mgr_init(void);
/**
 * @brief TODO: 描述axk_session_update_context的功能
 *
 * @param session_id TODO: 描述session_id
 * @param context TODO: 描述context
 * @return 0成功, -1失败
 */
int axk_session_update_context(const char *session_id, const char *context);
/**
 * @brief TODO: 描述axk_session_get_context的功能
 *
 * @param session_id TODO: 描述session_id
 * @param buf TODO: 描述buf
 * @param buf_size TODO: 描述buf_size
 * @return 0成功, -1失败
 */
int axk_session_get_context(const char *session_id, char *buf, size_t buf_size);

/* ── 会话上下文（多轮对话记忆） ───────────────── */

/**
 * @brief 从 session_mgr 恢复会话的 messages 历史
 *
 * @param session_id  会话标识符，格式 "<channel>:<chat_id>"（如 "websocket:client1"）
 * @return cJSON Array，失败或首轮对话返回新建的空数组（调用者负责 cJSON_Delete）
 */
cJSON *axk_session_load_messages(const char *session_id);

/**
 * @brief 将 messages 历史保存到 session_mgr（内部序列化 + easyflash 持久化）
 *
 * @param session_id  会话标识符
 * @param messages    cJSON Array（Anthropic content blocks 格式的 messages 数组）
 * @return 0 成功，-1 失败
 */
int axk_session_save_messages(const char *session_id, cJSON *messages);

/**
 * @brief 清理超过 idle_ticks 未活动的过期会话
 *
 * @param idle_ticks  FreeRTOS tick 值，last_active 早于此值的会话被清理
 */
void axk_session_cleanup_stale(uint32_t idle_ticks);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_SESSION_MGR_H */
