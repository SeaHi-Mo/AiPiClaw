/**
 * @file axk_hal_system.c
 * @brief system级硬件抽象层实现 - 接入 BL618 硬件 API
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 封装systemreset 、时钟、堆INFO、chipINFO etc核心system功能
 */

#include "axk_hal_system.h"

#include <stdio.h>
#include <string.h>

#if AXK_PLATFORM_BL618
    #include "bflb_mtimer.h"
    #include "arch/risc-v/t-head/Core/Include/core_rv32.h"
    #include "FreeRTOS.h"
    #include "mm.h"          /* kfree_size() — real heap free stats */
#elif AXK_PLATFORM_ESP32
    #include "esp_system.h"
    #include "esp_timer.h"
#endif

/* ── systemINFO ──────────────────────────────────── */

#define AXK_CHIP_NAME       "BL616/BL618"
#define AXK_SDK_VERSION     "bouffalo_sdk_v2.3"

/* ── public  API ────────────────────────────────────── */

/**
 * @brief initsystem HAL
 * @return OKreturn 0
 */
int axk_hal_system_init(void)
{
    AXK_LOG_INFO("[axk_hal_system] system HALinit, chip=%s, SDK=%s\r\n",
                 AXK_CHIP_NAME, AXK_SDK_VERSION);
    return 0;
}

/**
 * @brief systemreset 
 */
void axk_hal_system_reset(void)
{
    AXK_LOG_INFO("[axk_hal_system] systemreset ...\r\n");

#if AXK_PLATFORM_BL618
    csi_system_reset();
#elif AXK_PLATFORM_ESP32
    esp_restart();
#endif
    /* not 应 to 达here  */
    while (1) {}
}

/**
 * @brief get system运行time （毫s）
 * @return 运行time (ms)
 */
uint32_t axk_hal_system_get_time_ms(void)
{
#if AXK_PLATFORM_BL618
    return (uint32_t)bflb_mtimer_get_time_ms();
#elif AXK_PLATFORM_ESP32
    return (uint32_t)(esp_timer_get_time() / 1000);
#else
    return 0;
#endif
}

/**
 * @brief get system运行time （微s）
 * @return 运行time (us)
 */
uint32_t axk_hal_system_get_time_us(void)
{
#if AXK_PLATFORM_BL618
    return (uint32_t)bflb_mtimer_get_time_us();
#elif AXK_PLATFORM_ESP32
    return (uint32_t)esp_timer_get_time();
#else
    return 0;
#endif
}

/**
 * @brief get 剩余堆内存
 * @return 剩余堆size(bytes)
 */
uint32_t axk_hal_system_get_free_heap(void)
{
#if AXK_PLATFORM_BL618
    return (uint32_t)kfree_size(0);  /* heap_3 uses kmalloc, xPortGetFreeHeapSize is a 0-returning weak stub */
#elif AXK_PLATFORM_ESP32
    return esp_get_free_heap_size();
#else
    return 0;
#endif
}

/**
 * @brief get chipname 
 * @return chipname chars 串
 */
const char *axk_hal_system_get_chip_name(void)
{
    return AXK_CHIP_NAME;
}

/**
 * @brief get  SDK ver
 * @return SDKverchars 串
 */
const char *axk_hal_system_get_sdk_version(void)
{
    return AXK_SDK_VERSION;
}

/**
 * @brief 进入临界区（关断）
 */
void axk_hal_system_enter_critical(void)
{
#if AXK_PLATFORM_BL618
    __disable_irq();
#endif
}

/**
 * @brief 退出临界区（开断）
 */
void axk_hal_system_exit_critical(void)
{
#if AXK_PLATFORM_BL618
    __enable_irq();
#endif
}
