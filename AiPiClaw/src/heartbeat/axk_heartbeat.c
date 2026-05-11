/**
 * @file axk_heartbeat.c
 * @brief heartbeatmodule - 定期systemstatus report 
 * @version 2.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note use  axk_hal_timer 驱动周期性heartbeat，default per  60 sreport 一次
 *       report content 包括: 运行time 、剩余堆内存、WiFistatus 、message busstatus 
 */

#include "axk_heartbeat.h"
#include "axk_platform.h"
#include "axk_message_bus.h"
#include "axk_mimiclaw_port.h"
#include "axk_hal_timer.h"
#include "axk_hal_system.h"

#include <stdio.h>
#include <string.h>

#define HEARTBEAT_INTERVAL_MS  60000  /**< heartbeatinterval(ms)，default 60s */

static axk_timer_handle_t s_hb_timer = NULL;
static bool s_report_enabled = true;
static uint32_t s_uptime_sec = 0;

/* ── heartbeatcallback  ────────────────────────────────────── */

/* @brief TODO: 描述heartbeat_timer_cb的功能 @param timer TODO: 描述timer @param arg TODO: 描述arg @return 无返回值 */
static void heartbeat_timer_cb(axk_timer_handle_t timer, void *arg)
{
    char buf[512];
    uint32_t free_heap;
    const char *chip;
    const char *sdk;

    (void)timer;
    (void)arg;

    s_uptime_sec += (HEARTBEAT_INTERVAL_MS / 1000);

    if (!s_report_enabled) {
        return;
    }

    free_heap = axk_hal_system_get_free_heap();
    chip = axk_hal_system_get_chip_name();
    sdk = axk_hal_system_get_sdk_version();

    snprintf(buf, sizeof(buf),
             "[heartbeat] uptime=%lus heap=%luKB chip=%s sdk=%s",
             (unsigned long)s_uptime_sec,
             (unsigned long)(free_heap / 1024),
             chip ? chip : "?",
             sdk ? sdk : "?");

    AXK_LOG_INFO("%s\r\n", buf);

    /* 可选: 推入message bus供远程监控 */
    /* mimi_msg_t msg = {0};
       strncpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel)-1);
       msg.content = strdup(buf);
       msg.priority = MIMI_PRIO_LOW;
       axk_message_bus_push_outbound(&msg);
       free(msg.content); */
}

/* ── public  API ────────────────────────────────────── */

/**
 * @brief initheartbeatmodule
 * @return OKreturn 0
 */
int axk_heartbeat_init(void)
{
    axk_hal_timer_init();

    s_hb_timer = axk_hal_timer_create("heartbeat", HEARTBEAT_INTERVAL_MS,
                                       true, heartbeat_timer_cb, NULL);
    if (!s_hb_timer) {
        AXK_LOG_ERROR("[axk_heartbeat] createheartbeattimerFAIL\r\n");
        return -1;
    }

    axk_hal_timer_start(s_hb_timer);
    AXK_LOG_INFO("[axk_heartbeat] heartbeatmoduleinitok, interval=%ds\r\n",
                 HEARTBEAT_INTERVAL_MS / 1000);
    return 0;
}

/**
 * @brief heartbeatpoll ——in main loopcall 
 * @note 驱动 hal_timer check expirescallback 
 */
void axk_heartbeat_tick(void)
{
    axk_hal_timer_poll();
}

/**
 * @brief enable heartbeatreport 
 * @param[in] enable trueenable , falsedisable 
 */
void axk_heartbeat_set_report_enabled(bool enable)
{
    s_report_enabled = enable;
    AXK_LOG_INFO("[axk_heartbeat] report %s\r\n", enable ? "enable " : "disable ");
}

/**
 * @brief get system运行time 
 * @return 运行time (s)
 */
uint32_t axk_heartbeat_get_uptime_sec(void)
{
    return s_uptime_sec;
}
