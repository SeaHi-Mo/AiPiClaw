/**
 * @file axk_session_mgr.c
 * @brief session mgr器 - 内存缓存 + easyflash persist
 * @version 2.1
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 双层store: 内存数组(热data, O(1)访问) + easyflash blob(persist)
 *       reboot 后auto from  easyflash restore 最近会话上下文
 */

#include "axk_session_mgr.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "semphr.h"

#include <easyflash.h>
#include "cJSON.h"
#include "mimi_config.h"

#define AXK_MAX_SESSIONS      8     /**< 最大会话数 */
#define SESSION_KV_PREFIX     "sess_"  /**< easyflash key前缀 */

/**
 * 会话条目 (v2: 用固定大小 summary 替代堆分配的 context)
 */
typedef struct {
    bool     in_use;
    char     id[AXK_SESSION_ID_LEN];
    axk_context_summary_t summary;   /**< 结构化摘要 (栈内 2717 字节，替代原 16KB heap context) */
    uint32_t last_active;            /**< 最后活跃time (tick) */
} session_entry_t;

static session_entry_t s_sessions[AXK_MAX_SESSIONS];
static SemaphoreHandle_t s_session_mutex = NULL;
static bool s_initialized = false;

/* ── persisthelper  ────────────────────────────────── */

/**
 * @brief 持久化单个会话到 easyflash
 *
 * @param idx 会话槽位索引
 */
static void session_save_to_flash(int idx)
{
    char kv_key[AXK_SESSION_ID_LEN + 8];

    if (idx < 0 || idx >= AXK_MAX_SESSIONS) return;
    if (!s_sessions[idx].in_use) return;

    snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, s_sessions[idx].id);

    /* 序列化 summary 为 JSON 并写入 easyflash */
    cJSON *json = axk_context_summary_to_json(&s_sessions[idx].summary);
    if (!json) return;

    char *json_str = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!json_str) return;

    size_t len = strlen(json_str);
    if (ef_set_env_blob(kv_key, json_str, len + 1) == EF_NO_ERR) {
        ef_save_env();
    }
    free(json_str);
}

/**
 * @brief 从 easyflash 恢复所有会话（按需加载）
 *
 * @return 无返回值
 */
static void session_load_all_from_flash(void)
{
    /* easyflash not support 遍历，按需load；
       here ensure moduleready 即可，实际datain  get/update 时trigger  */
}

/* ── internalfind  ────────────────────────────────────── */

/**
 * @brief 查找会话索引
 *
 * @param session_id 会话标识符
 * @return 槽位索引，-1 表示未找到
 */
static int session_find(const char *session_id)
{
    int i;
    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        if (s_sessions[i].in_use &&
            strcmp(s_sessions[i].id, session_id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief 分配会话槽位（优先空闲槽位，满时 LRU 淘汰最旧会话）
 *
 * @param session_id 会话标识符
 * @return 槽位索引，-1 表示分配失败
 */
static int session_alloc(const char *session_id)
{
    int i;
    /* 先找empty 闲槽位 */
    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        if (!s_sessions[i].in_use) {
            memset(&s_sessions[i], 0, sizeof(s_sessions[i]));
            strncpy(s_sessions[i].id, session_id, AXK_SESSION_ID_LEN - 1);
            s_sessions[i].in_use = true;
            return i;
        }
    }
    /* full , replace 最旧 */
    uint32_t oldest = UINT32_MAX;
    int oldest_idx = -1;
    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        if (s_sessions[i].last_active < oldest) {
            oldest = s_sessions[i].last_active;
            oldest_idx = i;
        }
    }
    if (oldest_idx >= 0) {
        memset(&s_sessions[oldest_idx], 0, sizeof(s_sessions[oldest_idx]));
        strncpy(s_sessions[oldest_idx].id, session_id, AXK_SESSION_ID_LEN - 1);
        s_sessions[oldest_idx].in_use = true;
    }
    return oldest_idx;
}

/* ── public  API ────────────────────────────────────── */

/**
 * @brief 初始化 session_mgr 模块
 *
 * @return 0成功, -1失败
 */
int axk_session_mgr_init(void)
{
    if (s_initialized) return 0;

    memset(s_sessions, 0, sizeof(s_sessions));

    s_session_mutex = xSemaphoreCreateMutex();
    if (!s_session_mutex) {
        AXK_LOG_ERROR("[axk_session_mgr] mutex createFAIL\r\n");
        return -1;
    }

    session_load_all_from_flash();
    s_initialized = true;

    AXK_LOG_INFO("[axk_session_mgr] initok, 最大%d会话 + easyflashpersist\r\n",
                 AXK_MAX_SESSIONS);
    return 0;
}

/**
 * @brief 更新会话上下文
 *
 * @param session_id 会话标识符
 * @param context 上下文内容
 * @return 0成功, -1失败
 */
int axk_session_update_context(const char *session_id, const char *context)
{
    int idx;

    if (!session_id || !context || !s_initialized) return -1;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    idx = session_find(session_id);
    if (idx < 0) {
        idx = session_alloc(session_id);
    }

    if (idx < 0) {
        xSemaphoreGive(s_session_mutex);
        return -1;
    }

    /* v2: summary 模式下 update_context 仅更新元数据 (context 文本不再直存) */
    s_sessions[idx].last_active = xTaskGetTickCount();
    session_save_to_flash(idx);

    xSemaphoreGive(s_session_mutex);
    return 0;
}

/**
 * @brief 获取会话上下文
 *
 * @param session_id 会话标识符
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 0成功, -1失败
 */
int axk_session_get_context(const char *session_id, char *buf, size_t buf_size)
{
    int i;

    if (!session_id || !buf || buf_size == 0 || !s_initialized) return -1;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    /* v2: 从 summary 提取上下文文本 */
    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        if (s_sessions[i].in_use &&
            strcmp(s_sessions[i].id, session_id) == 0) {
            axk_context_summary_format_for_prompt(&s_sessions[i].summary, buf, buf_size);
            s_sessions[i].last_active = xTaskGetTickCount();
            xSemaphoreGive(s_session_mutex);
            return 0;
        }
    }

    xSemaphoreGive(s_session_mutex);
    buf[0] = '\0';
    return -1;
}

/* ── 多轮对话记忆 API ──────────────────────────────── */

/**
 * @brief 从 session_mgr 恢复会话的 messages 历史
 *
 * @param session_id  会话标识符，格式 "<channel>:<chat_id>"（如 "websocket:client1"）
 * @return cJSON Array，失败或首轮对话返回新建的空数组（调用者负责 cJSON_Delete）
 */
cJSON *axk_session_load_messages(const char *session_id)
{
    int idx;
    cJSON *messages;

    if (!session_id) return cJSON_CreateArray();

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return cJSON_CreateArray();
    }

    idx = session_find(session_id);
    if (idx < 0) {
        /* 内存未命中 → 尝试从 easyflash 恢复 */
        char kv_key[AXK_SESSION_ID_LEN + 8];
        snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, session_id);
        size_t len;
        char buf[MIMI_CONTEXT_BUF_SIZE];
        if (ef_get_env_blob(kv_key, buf, sizeof(buf), &len) > 0) {
            /* 尝试反序列化摘要 JSON */
            cJSON *json = cJSON_Parse(buf);
            if (json && cJSON_IsObject(json)) {
                idx = session_alloc(session_id);
                if (idx >= 0) {
                    if (axk_context_summary_from_json(json, &s_sessions[idx].summary)) {
                        s_sessions[idx].last_active = xTaskGetTickCount();
                    }
                }
                cJSON_Delete(json);
            } else if (json) {
                cJSON_Delete(json);
                /* 旧版 messages JSON 格式 — 尝试解析为 messages 数组并蒸馏 */
                json = cJSON_Parse(buf);
                if (json && cJSON_IsArray(json)) {
                    idx = session_alloc(session_id);
                    if (idx >= 0) {
                        axk_context_summarize(json, &s_sessions[idx].summary);
                        s_sessions[idx].last_active = xTaskGetTickCount();
                    }
                    cJSON_Delete(json);
                } else if (json) {
                    cJSON_Delete(json);
                }
            }
        }
    }

    if (idx < 0) {
        xSemaphoreGive(s_session_mutex);
        return cJSON_CreateArray();
    }

    s_sessions[idx].last_active = xTaskGetTickCount();

    /* v2: 上下文摘要由 build_system_prompt() 通过 system prompt 注入，messages 只放真实对话交换 */
    messages = cJSON_CreateArray();

    xSemaphoreGive(s_session_mutex);
    return messages;
}

/**
 * @brief 将 messages 历史保存到 session_mgr（内部序列化 + easyflash 持久化）
 *
 * @param session_id  会话标识符
 * @param messages    cJSON Array（Anthropic content blocks 格式的 messages 数组）
 * @return 0 成功，-1 失败
 */
int axk_session_save_messages(const char *session_id, cJSON *messages)
{
    int idx;

    if (!session_id || !messages) return -1;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    /* 查找或创建会话 */
    idx = session_find(session_id);
    if (idx < 0) {
        idx = session_alloc(session_id);
    }

    if (idx < 0) {
        xSemaphoreGive(s_session_mutex);
        return -1;
    }

    /* v2: 蒸馏 messages 为摘要并持久化 */
    axk_context_summarize(messages, &s_sessions[idx].summary);
    s_sessions[idx].last_active = xTaskGetTickCount();
    session_save_to_flash(idx);

    xSemaphoreGive(s_session_mutex);
    return 0;
}

/**
 * @brief 清理超过 idle_ticks 未活动的过期会话
 *
 * @param idle_ticks  FreeRTOS tick 值，last_active 早于此值的会话被清理
 */
void axk_session_cleanup_stale(uint32_t idle_ticks)
{
    uint32_t now = xTaskGetTickCount();
    int i;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        session_entry_t *entry = &s_sessions[i];
        if (!entry->in_use) continue;
        if ((now - entry->last_active) > idle_ticks) {
            entry->id[0] = '\0';
            entry->in_use = false;
        }
    }

    xSemaphoreGive(s_session_mutex);
}

/* ── 上下文摘要 API (v2) ──────────────────────────────── */

/**
 * @brief 将 messages 蒸馏为摘要并保存到 session_mgr
 */
int axk_session_save_summary(const char *session_id, cJSON *messages)
{
    if (!session_id || !messages) return -1;

    /* 直接复用 save_messages (内部已调用 axk_context_summarize) */
    return axk_session_save_messages(session_id, messages);
}

/**
 * @brief 从 session_mgr 加载结构化摘要
 */
int axk_session_load_summary(const char *session_id, axk_context_summary_t *summary)
{
    int idx;

    if (!session_id || !summary || !s_initialized) return -1;
    axk_context_summary_init(summary);

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    idx = session_find(session_id);
    if (idx < 0) {
        /* 尝试从 easyflash 加载 */
        char kv_key[AXK_SESSION_ID_LEN + 8];
        snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, session_id);
        size_t len;
        char buf[MIMI_CONTEXT_BUF_SIZE];
        if (ef_get_env_blob(kv_key, buf, sizeof(buf), &len) > 0) {
            cJSON *json = cJSON_Parse(buf);
            if (json && cJSON_IsObject(json)) {
                idx = session_alloc(session_id);
                if (idx >= 0) {
                    if (axk_context_summary_from_json(json, &s_sessions[idx].summary)) {
                        s_sessions[idx].last_active = xTaskGetTickCount();
                    }
                }
                cJSON_Delete(json);
                if (idx >= 0) {
                    memcpy(summary, &s_sessions[idx].summary, sizeof(*summary));
                    xSemaphoreGive(s_session_mutex);
                    return 0;
                }
            } else if (json) {
                cJSON_Delete(json);
            }
        }
        xSemaphoreGive(s_session_mutex);
        return -1;
    }

    s_sessions[idx].last_active = xTaskGetTickCount();
    memcpy(summary, &s_sessions[idx].summary, sizeof(*summary));
    xSemaphoreGive(s_session_mutex);
    return 0;
}
