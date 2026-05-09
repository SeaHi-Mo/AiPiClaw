/**
 * @file axk_agent_loop.h
 * @brief agent_loop module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_AGENT_LOOP_H
#define __AXK_AGENT_LOOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_agent_loop_init(void);
void axk_agent_loop_run(void);
int axk_agent_loop_start(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_AGENT_LOOP_H */
