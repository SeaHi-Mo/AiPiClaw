/**
 * @file axk_tool_files.h
 * @brief tool_files module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_FILES_H
#define __AXK_TOOL_FILES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_tool_files_init(void);
int axk_tool_read_file_execute(const char *input_json, char *output, size_t output_size);
int axk_tool_write_file_execute(const char *input_json, char *output, size_t output_size);
int axk_tool_edit_file_execute(const char *input_json, char *output, size_t output_size);
int axk_tool_list_dir_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief get mount  LittleFS 实eg.
 * @return LittleFS 实eg.ptr ，not mountedreturn  NULL
 * @note 供 skill_loader  etcmodule进行directory 遍历use 
 */
struct lfs *axk_tool_files_get_lfs(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_FILES_H */
