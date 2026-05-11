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

/**
 * @brief 初始化文件工具模块，挂载LittleFS和/或FATFS文件系统
 *
 * @return 0成功, -1失败
 */
int axk_tool_files_init(void);
/**
 * @brief 执行读文件操作：根据JSON输入中的path字段读取指定路径的文件内容
 *
 * @param input_json JSON输入，需包含"path"字段指定文件路径
 * @param output 输出缓冲区，用于存放文件内容
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_read_file_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 执行写文件操作：根据JSON输入将content内容写入path指定路径的文件
 *
 * @param input_json JSON输入，需包含"path"和"content"字段
 * @param output 输出缓冲区，用于存放操作结果
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_write_file_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 执行文件编辑操作：在指定文件中查找old_string并替换为new_string
 *
 * @param input_json JSON输入，需包含"path"、"old_string"和"new_string"字段
 * @param output 输出缓冲区，用于存放操作结果
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_edit_file_execute(const char *input_json, char *output, size_t output_size);
/**
 * @brief 执行目录列表操作：遍历文件系统并列出匹配指定前缀的文件
 *
 * @param input_json JSON输入，可选"prefix"字段用于过滤文件路径
 * @param output 输出缓冲区，用于存放文件路径列表
 * @param output_size 输出缓冲区大小（字节）
 * @return 0成功, -1失败
 */
int axk_tool_list_dir_execute(const char *input_json, char *output, size_t output_size);

/**
 * @brief get mount  LittleFS 实eg.
 *
 * @return LittleFS 实eg.ptr ，not mountedreturn  NULL
 * @note 供 skill_loader  etcmodule进行directory 遍历use 
 */
struct lfs *axk_tool_files_get_lfs(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_FILES_H */
