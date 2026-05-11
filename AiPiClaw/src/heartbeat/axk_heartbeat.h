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

/* @brief TODO: 描述axk_heartbeat_init的功能 @return 0成功, -1失败 */
int axk_heartbeat_init(void);
/* @brief TODO: 描述axk_heartbeat_tick的功能 @return 无返回值 */
void axk_heartbeat_tick(void);
/* @brief TODO: 描述axk_heartbeat_set_report_enabled的功能 @param enable TODO: 描述enable @return 无返回值 */
void axk_heartbeat_set_report_enabled(bool enable);
/* @brief TODO: 描述axk_heartbeat_get_uptime_sec的功能 @return 0成功, -1失败 */
uint32_t axk_heartbeat_get_uptime_sec(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_HEARTBEAT_H */
