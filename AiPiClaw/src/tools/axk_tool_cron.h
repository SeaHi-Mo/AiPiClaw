/**
 * @file axk_tool_cron.h
 * @brief crontasktool头file
 * @version 1.0
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_CRON_H
#define __AXK_TOOL_CRON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 添加crontasktool
 * @param[in] input_json input JSONchars 串
 * @param[out] output output buffer 
 * @param[in] output_size output buffer size
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_tool_cron_add_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief 列出allcrontasktool
 * @param[in] input_json input JSONchars 串（可为empty 对象）
 * @param[out] output output buffer 
 * @param[in] output_size output buffer size
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_tool_cron_list_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief delete crontasktool
 * @param[in] input_json input JSONchars 串（含job_id）
 * @param[out] output output buffer 
 * @param[in] output_size output buffer size
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_tool_cron_remove_execute(const char *input_json, char *output, size_t output_size);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_CRON_H */
