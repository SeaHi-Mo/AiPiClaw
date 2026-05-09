#ifndef __AXK_COMPAT_FREERTOS_PORTABLE_H
#define __AXK_COMPAT_FREERTOS_PORTABLE_H

/**
 * @file portable.h
 * @brief FreeRTOS 移植层补充 - provide MimiClaw所需extra 符号
 * @version 1.0
 * @date 2026-04-20
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include <stddef.h>
#include <stdint.h>

/* BL618 SDK  FreeRTOS may use not 同堆func 名 */
/* 果实际not 存in ，then provide 桩实现 */

#ifdef __cplusplus
extern "C" {
#endif

/* FreeRTOS 堆mgr兼容 */
#ifndef xPortGetFreeHeapSize
size_t xPortGetFreeHeapSize(void);
#endif

#ifndef xPortGetMinimumEverFreeHeapSize
size_t xPortGetMinimumEverFreeHeapSize(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __AXK_COMPAT_FREERTOS_PORTABLE_H */
