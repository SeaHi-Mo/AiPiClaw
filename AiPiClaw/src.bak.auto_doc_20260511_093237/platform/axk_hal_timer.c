/**
 * @file axk_hal_timer.c
 * @brief timer硬件抽象层实现 - 基于 bflb_mtimer 软件timer池
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 使用 bflb_mtimer_get_time_ms() 作为时间基准，在 axk_hal_timer_poll()
 *       轮询触发到期定时器。支持单次和自动重载模式。
 *       最大支持 AXK_TIMER_POOL_SIZE 个并发定时器。
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

#define AXK_TIMER_POOL_SIZE   8  /**< 最大并发定时器数量 */
#define AXK_TIMER_NAME_MAX   16  /**< 定时器名称最大长度 */

/** 定时器条目 */
typedef struct {
    char         name[AXK_TIMER_NAME_MAX]; /**< 定时器名称 */
    uint32_t     period_ms;    /**< 周期(毫秒) */
    bool         auto_reload;  /**< 是否自动重载 */
    bool         active;       /**< 是否激活 */
    uint64_t     next_expire;  /**< 下次到期时间(毫秒时间戳) */
    axk_timer_cb_t cb;         /**< 回调函数 */
    void        *arg;          /**< 回调参数 */
} axk_timer_entry_t;

static axk_timer_entry_t s_timers[AXK_TIMER_POOL_SIZE];
static bool s_initialized = false;

/**
 * @brief 获取当前系统时间戳（毫秒）
 * @return 当前毫秒时间戳
 */
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
 * @brief 初始化定时器模块
 * @return 0 成功
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
 * @brief 创建定时器
 * @param[in] name 定时器名称（用于调试）
 * @param[in] period_ms 周期(毫秒)
 * @param[in] auto_reload true=周期定时，false=单次定时
 * @param[in] cb 到期回调函数
 * @param[in] arg 回调参数
 * @return 定时器句柄，失败返回NULL
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
            s_timers[i].next_expire = 0;  /**< not start, start时set  */

            if (name) {
                strncpy(s_timers[i].name, name, AXK_TIMER_NAME_MAX - 1);
                s_timers[i].name[AXK_TIMER_NAME_MAX - 1] = '\0';
            } else {
                snprintf(s_timers[i].name, sizeof(s_timers[i].name), "timer_%d", i);
            }

            AXK_LOG_DEBUG("[axk_hal_timer] createtimer '%s' period=%ums reload=%d\r\n",
                          s_timers[i].name, (int)period_ms, (int)auto_reload);
            return (axk_timer_handle_t)(uintptr_t)(i + 1);  /**< return  1-based index  */
        }
    }

    AXK_LOG_ERROR("[axk_hal_timer] timer池full (%d)\r\n", AXK_TIMER_POOL_SIZE);
    return NULL;
}

/**
 * @brief 启动定时器
 * @param[in] timer 定时器句柄
 * @return 0 成功
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
