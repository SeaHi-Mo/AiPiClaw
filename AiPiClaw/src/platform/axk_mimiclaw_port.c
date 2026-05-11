/**
 * @file axk_mimiclaw_port.c
 * @brief 平台通用接口 - 安信可科技 BL618 port
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#include "axk_mimiclaw_port.h"

#include "FreeRTOS.h"
#include "task.h"
#include "bflb_mtimer.h"

/**
 * @brief get system运行time （毫s）
 *
 * @return 运行time ，单位毫s
 */
uint32_t axk_mimiclaw_port_uptime_ms(void)
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

/**
 * @brief 延时指定毫s数
 *
 * @param[in] ms 延时time ，单位毫s
 * @note if FreeRTOS调度器正in 运行then use vTaskDelay，否then use 硬件延时
 */
void axk_mimiclaw_port_sleep_ms(uint32_t ms)
{
    BaseType_t sched = xTaskGetSchedulerState();

    if (sched == taskSCHEDULER_RUNNING) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }

    /* 某些callback in 调度器挂起时运行，此时not 能use vTaskDelay */
    bflb_mtimer_delay_ms(ms);
}

/**
 * @brief get random 数
 *
 * @return 32位random 数
 * @note 基于bflb硬件random 数生成器
 */
uint32_t axk_mimiclaw_port_random(void)
{
    static uint32_t seed = 0;
    if (seed == 0) {
        seed = bflb_mtimer_get_time_us();
    }
    seed = (seed * 1103515245u + 12345u) & 0x7fffffff;
    return seed;
}

/**
 * @brief compute 两time 戳between 差value （带溢出保护）
 *
 * @param[in] later 较晚time 戳
 * @param[in] earlier 较早time 戳
 * @return time 差，单位毫s
 */
uint32_t axk_mimiclaw_port_time_diff(uint32_t later, uint32_t earlier)
{
    return later - earlier;
}
