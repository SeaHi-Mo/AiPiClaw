/**
 * @file axk_hal_timer.c
 * @brief timer硬件抽象层实现 - 基于 bflb_mtimer 软件timer池
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note use  bflb_mtimer_get_time_ms() 作为time 基准，in  axk_hal_timer_poll() 
 *       poll trigger expirestimer。support 单次 & auto 重载mode 。
 *       最大support  AXK_TIMER_POOL_SIZE  and 发timer。
 */

#include "axk_hal_timer.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if AXK_PLATFORM_BL618
    #include "bflb_mtimer.h"
#elif AXK_PLATFORM_ESP32
    #include "esp_timer.h"
#endif

#define AXK_TIMER_POOL_SIZE   8  /**< 最大 and 发timercount */
#define AXK_TIMER_NAME_MAX   16  /**< timername 最大length */

/**
 * timer条目
 */
typedef struct {
    char         name[AXK_TIMER_NAME_MAX]; /**< timername */
    uint32_t     period_ms;    /**< cron周期(ms) */
    bool         auto_reload;  /**< is否auto 重载 */
    bool         active;       /**< is否active */
    uint64_t     next_expire;  /**< 下次expirestime (mstime 戳) */
    axk_timer_cb_t cb;         /**< callback func */
    void        *arg;          /**< callback param */
} axk_timer_entry_t;

static axk_timer_entry_t s_timers[AXK_TIMER_POOL_SIZE];
static bool s_initialized = false;

/* ── get current time 戳 ──────────────────────────────── */

/* @brief TODO: 描述axk_timer_now_ms的功能 @return 0成功, -1失败 */
static uint64_t axk_timer_now_ms(void)
{
#if AXK_PLATFORM_BL618
    return bflb_mtimer_get_time_ms();
#elif AXK_PLATFORM_ESP32
    return (uint64_t)(esp_timer_get_time() / 1000);
#else
    return 0;
#endif
}

/* ── public  API ────────────────────────────────────── */

/**
 * @brief inittimermodule
 *
 * @return OKreturn 0
 */
int axk_hal_timer_init(void)
{
    if (s_initialized) {
        return 0;
    }
    memset(s_timers, 0, sizeof(s_timers));
    s_initialized = true;
    AXK_LOG_INFO("[axk_hal_timer] timerHALinitok, 池size=%d\r\n", AXK_TIMER_POOL_SIZE);
    return 0;
}

/**
 * @brief createtimer
 *
 * @param[in] name timername （for debug ）
 * @param[in] period_ms cron周期(毫s)
 * @param[in] auto_reload true=周期cron，false=单次cron
 * @param[in] cb expirescallback func 
 * @param[in] arg callback param 
 * @return timerhandle ，FAILreturn NULL
 */
axk_timer_handle_t axk_hal_timer_create(const char *name, uint32_t period_ms,
                                         bool auto_reload, axk_timer_cb_t cb, void *arg)
{
    int i;

    if (!s_initialized || period_ms == 0) {
        return NULL;
    }

    /* find empty 闲槽位 */
    for (i = 0; i < AXK_TIMER_POOL_SIZE; i++) {
        if (!s_timers[i].active) {
            s_timers[i].active = true;
            s_timers[i].period_ms = period_ms;
            s_timers[i].auto_reload = auto_reload;
            s_timers[i].cb = cb;
            s_timers[i].arg = arg;
            s_timers[i].next_expire = 0;  /**< not start, start时set */

            if (name) {
                strncpy(s_timers[i].name, name, AXK_TIMER_NAME_MAX - 1);
                s_timers[i].name[AXK_TIMER_NAME_MAX - 1] = '\0';
            } else {
                snprintf(s_timers[i].name, sizeof(s_timers[i].name), "timer_%d", i);
            }

            AXK_LOG_DEBUG("[axk_hal_timer] createtimer '%s' period=%ums reload=%d\r\n",
                          s_timers[i].name, (int)period_ms, (int)auto_reload);
            return (axk_timer_handle_t)(uintptr_t)(i + 1);  /**< return  1-based index */
        }
    }

    AXK_LOG_ERROR("[axk_hal_timer] timer池full (%d)\r\n", AXK_TIMER_POOL_SIZE);
    return NULL;
}

/**
 * @brief start timer
 *
 * @param[in] timer timerhandle 
 * @return OKreturn 0
 */
int axk_hal_timer_start(axk_timer_handle_t timer)
{
    int idx;

    if (!timer) return -1;
    idx = (int)((uintptr_t)timer) - 1;

    if (idx < 0 || idx >= AXK_TIMER_POOL_SIZE || !s_timers[idx].active) {
        return -1;
    }

    s_timers[idx].next_expire = axk_timer_now_ms() + s_timers[idx].period_ms;
    AXK_LOG_DEBUG("[axk_hal_timer] start timer '%s' expires=%llums\r\n",
                  s_timers[idx].name, (unsigned long long)s_timers[idx].next_expire);
    return 0;
}

/**
 * @brief stop timer
 *
 * @param[in] timer timerhandle 
 * @return OKreturn 0
 */
int axk_hal_timer_stop(axk_timer_handle_t timer)
{
    int idx;

    if (!timer) return -1;
    idx = (int)((uintptr_t)timer) - 1;

    if (idx < 0 || idx >= AXK_TIMER_POOL_SIZE) {
        return -1;
    }

    s_timers[idx].next_expire = 0;
    AXK_LOG_DEBUG("[axk_hal_timer] stop timer '%s'\r\n", s_timers[idx].name);
    return 0;
}

/**
 * @brief delete timer and release 资源
 *
 * @param[in] timer timerhandle 
 * @return OKreturn 0
 */
int axk_hal_timer_delete(axk_timer_handle_t timer)
{
    int idx;

    if (!timer) return -1;
    idx = (int)((uintptr_t)timer) - 1;

    if (idx < 0 || idx >= AXK_TIMER_POOL_SIZE) {
        return -1;
    }

    AXK_LOG_DEBUG("[axk_hal_timer] delete timer '%s'\r\n", s_timers[idx].name);
    memset(&s_timers[idx], 0, sizeof(s_timers[idx]));
    return 0;
}

/**
 * @brief reset timer（重新start计时）
 *
 * @param[in] timer timerhandle 
 * @return OKreturn 0
 */
int axk_hal_timer_reset(axk_timer_handle_t timer)
{
    int idx;

    if (!timer) return -1;
    idx = (int)((uintptr_t)timer) - 1;

    if (idx < 0 || idx >= AXK_TIMER_POOL_SIZE || !s_timers[idx].active) {
        return -1;
    }

    s_timers[idx].next_expire = axk_timer_now_ms() + s_timers[idx].period_ms;
    return 0;
}

/**
 * @brief timerpoll  - in main loopcall ，check  and trigger expirestimer
 *
 * @note 应in per main loop迭代call 一次
 */
void axk_hal_timer_poll(void)
{
    uint64_t now;
    int i;

    if (!s_initialized) return;

    now = axk_timer_now_ms();

    for (i = 0; i < AXK_TIMER_POOL_SIZE; i++) {
        if (!s_timers[i].active) continue;
        if (s_timers[i].next_expire == 0) continue;

        if (now >= s_timers[i].next_expire) {
            /* trigger callback  */
            axk_timer_cb_t cb = s_timers[i].cb;
            void *arg = s_timers[i].arg;

            if (s_timers[i].auto_reload) {
                /* 周期mode ：重新set 下次expirestime  */
                s_timers[i].next_expire = now + s_timers[i].period_ms;
            } else {
                /* 单次mode ：标记为expires */
                s_timers[i].next_expire = 0;
            }

            /* call callback （in status update 后，允许callback 修改timer） */
            if (cb) {
                axk_timer_handle_t handle = (axk_timer_handle_t)(uintptr_t)(i + 1);
                cb(handle, arg);
            }
        }
    }
}
