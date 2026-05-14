/**
 * @file axk_tool_registry.h
 * @brief tool registrymodule - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-23
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TOOL_REGISTRY_H
#define __AXK_TOOL_REGISTRY_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief tooldescription 结构体
 */
typedef struct {
    const char *name;                       /**< toolname */
    const char *description;                /**< tooldescription */
    const char *input_schema_json;          /**< input param JSON Schemachars 串 */
    int (*execute)(const char *input_json, char *output, size_t output_size); /**< 执行func */
} mimi_tool_t;

/**
 * @brief inittool registry，registerallbuiltin tool
 *
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_tool_registry_init(void);

/**
 * @brief get toolJSON数组chars 串，for LLM APIrequest
 *
 * @return JSONchars 串ptr ，无tool时return NULL
 * @note return ptr 为internal缓存，not 应call 者release 
 */
const char *axk_tool_registry_get_tools_json(void);

/**
 * @brief 按name 执行tool
 *
 * @param[in] name toolname 
 * @param[in] input_json input JSONchars 串
 * @param[out] output output buffer 
 * @param[in] output_size output buffer size
 * @return OKreturn 0，not 找 to toolreturn -1
 */
int axk_tool_registry_execute(const char *name, const char *input_json,
                              char *output, size_t output_size);

/**
 * @brief 注册单个工具到注册表（供外部模块调用）
 * @param[in] tool 工具描述结构体指针
 */
void axk_register_tool(const mimi_tool_t *tool);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TOOL_REGISTRY_H */
