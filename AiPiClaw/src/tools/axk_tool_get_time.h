/**
 * @file axk_tool_get_time.h
 * @brief tool_get_time module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_GET_TIME_H
#define __AXK_TOOL_GET_TIME_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化时间获取工具，注册时间查询执行接口
 *
 * @return 0成功, -1失败
 */
int axk_tool_get_time_init(void);
/**
 * @brief 执行获取当前UTC时间的操作：优先使用系统RTC，必要时通过NTP同步
 *
 * @param input_json JSON输入（可选，包含"sync"布尔字段强制NTP同步）
 * @param output 输出缓冲区，用于存放UTC时间字符串
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_get_time_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_GET_TIME_H */
