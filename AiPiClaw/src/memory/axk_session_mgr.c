/**
 * @file axk_session_mgr.c
 * @brief session mgr器 - 内存缓存 + easyflash persist
 * @version 2.0
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

#define AXK_MAX_SESSIONS      8     /**< 最大会话数 */
#define AXK_SESSION_CTX_LEN   512   /**< 上下文最大length */
#define AXK_SESSION_ID_LEN    64    /**< 会话ID最大length */
#define SESSION_KV_PREFIX     "sess_"  /**< easyflash key前缀 */

/**
 * 会话条目
 */
typedef struct {
    bool     in_use /*< TODO: 描述in_use */;
    char     id[AXK_SESSION_ID_LEN] /*< TODO: 描述id */;
    char     context[AXK_SESSION_CTX_LEN] /*< TODO: 描述context */;
    uint32_t last_active;          /**< 最后活跃time (tick) */
} session_entry_t;

static session_entry_t s_sessions[AXK_MAX_SESSIONS];
static SemaphoreHandle_t s_session_mutex = NULL;
static bool s_initialized = false;

/* ── persisthelper  ────────────────────────────────── */

/**
 * @brief TODO: 描述session_save_to_flash的功能
 *
 * @param idx TODO: 描述idx
 * @return 无返回值
 */
static void session_save_to_flash(int idx)
{
    char kv_key[AXK_SESSION_ID_LEN + 8];

    if (idx < 0 || idx >= AXK_MAX_SESSIONS) return;
    if (!s_sessions[idx].in_use) return;

    snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, s_sessions[idx].id);

    if (ef_set_env_blob(kv_key, s_sessions[idx].context,
                        strlen(s_sessions[idx].context) + 1) == EF_NO_ERR) {
        ef_save_env();
    }
}

/**
 * @brief TODO: 描述session_load_all_from_flash的功能
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
 * @brief TODO: 描述session_find的功能
 *
 * @param session_id TODO: 描述session_id
 * @return 0成功, -1失败
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
 * @brief TODO: 描述session_alloc的功能
 *
 * @param session_id TODO: 描述session_id
 * @return 0成功, -1失败
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
 * @brief TODO: 描述axk_session_mgr_init的功能
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
 * @brief TODO: 描述axk_session_update_context的功能
 *
 * @param session_id TODO: 描述session_id
 * @param context TODO: 描述context
 * @return 0成功, -1失败
 */
int axk_session_update_context(const char *session_id, const char *context)
{
    int idx;
    bool loaded_from_flash = false;

    if (!session_id || !context || !s_initialized) return -1;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    idx = session_find(session_id);
    if (idx < 0) {
        /* attempt from  easyflash load */
        char kv_key[AXK_SESSION_ID_LEN + 8];
        snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, session_id);
        size_t len;
        char buf[AXK_SESSION_CTX_LEN];
        if (ef_get_env_blob(kv_key, buf, sizeof(buf), &len) > 0) {
            /* 找 to persistdata, create内存条目 */
            idx = session_alloc(session_id);
            if (idx >= 0) {
                strncpy(s_sessions[idx].context, buf, AXK_SESSION_CTX_LEN - 1);
                loaded_from_flash = true;
            }
        }
    }

    if (idx < 0) {
        idx = session_alloc(session_id);
    }

    if (idx < 0) {
        xSemaphoreGive(s_session_mutex);
        return -1;
    }

    /* update 上下文 */
    if (!loaded_from_flash || strcmp(s_sessions[idx].context, context) != 0) {
        strncpy(s_sessions[idx].context, context, AXK_SESSION_CTX_LEN - 1);
        s_sessions[idx].context[AXK_SESSION_CTX_LEN - 1] = '\0';
        s_sessions[idx].last_active = xTaskGetTickCount();
        session_save_to_flash(idx);
    }

    xSemaphoreGive(s_session_mutex);
    return 0;
}

/**
 * @brief TODO: 描述axk_session_get_context的功能
 *
 * @param session_id TODO: 描述session_id
 * @param buf TODO: 描述buf
 * @param buf_size TODO: 描述buf_size
 * @return 0成功, -1失败
 */
int axk_session_get_context(const char *session_id, char *buf, size_t buf_size)
{
    int i;

    if (!session_id || !buf || buf_size == 0 || !s_initialized) return -1;

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    /* 先in 内存find  */
    for (i = 0; i < AXK_MAX_SESSIONS; i++) {
        if (s_sessions[i].in_use &&
            strcmp(s_sessions[i].id, session_id) == 0) {
            strncpy(buf, s_sessions[i].context, buf_size - 1);
            buf[buf_size - 1] = '\0';
            s_sessions[i].last_active = xTaskGetTickCount();
            xSemaphoreGive(s_session_mutex);
            return 0;
        }
    }

    /* 内存not 找 to ，attempt from  easyflash load */
    char kv_key[AXK_SESSION_ID_LEN + 8];
    snprintf(kv_key, sizeof(kv_key), "%s%s", SESSION_KV_PREFIX, session_id);
    size_t len;
    if (ef_get_env_blob(kv_key, buf, buf_size, &len) > 0) {
        buf[buf_size - 1] = '\0';
        xSemaphoreGive(s_session_mutex);
        return 0;
    }

    xSemaphoreGive(s_session_mutex);
    buf[0] = '\0';
    return -1;
}
