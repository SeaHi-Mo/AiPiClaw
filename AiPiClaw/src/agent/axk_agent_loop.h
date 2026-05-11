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

/**
 * @brief 初始化Agent循环模块
 *
 * @return 成功返回0
 */
int axk_agent_loop_init(void);
/**
 * @brief Agent循环运行入口（API兼容保留，实际由start创建独立FreeRTOS任务执行）
 *
 * @return 无返回值
 */
void axk_agent_loop_run(void);
/**
 * @brief 启动Agent循环FreeRTOS任务（仅首次调用有效）
 *
 * @return 成功返回0，创建失败返回-1
 */
int axk_agent_loop_start(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_AGENT_LOOP_H */
