/**
 * @file heap_compat.c
 * @brief FreeRTOS 堆兼容层 - provide xPortGetFreeHeapSize etcfunc 实现
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include <stddef.h>

/* 果SDKprovide 该func ，then 弱实现会覆盖 */
__attribute__((weak)) size_t xPortGetFreeHeapSize(void)
{
    return 0;
}

__attribute__((weak)) size_t xPortGetMinimumEverFreeHeapSize(void)
{
    return 0;
}
