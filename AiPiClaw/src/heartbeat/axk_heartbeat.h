/**
 * @file axk_heartbeat.h
 * @brief heartbeat module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_HEARTBEAT_H
#define __AXK_HEARTBEAT_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_heartbeat_init(void);
void axk_heartbeat_tick(void);
void axk_heartbeat_set_report_enabled(bool enable);
uint32_t axk_heartbeat_get_uptime_sec(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_HEARTBEAT_H */
