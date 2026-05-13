/**
 * @file axk_context_summary.h
 * @brief 上下文摘要数据结构 — 替代 messages 直通，防止旧 Q&A 重复注入
 * @version 1.0
 * @date 2026-05-13
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 蒸馏策略: 从 messages 数组中提取身份/事实/最近交换，固化到固定大小结构体
 *       RAM 占用 ~2717 字节，easyflash blob <2KB
 */

#ifndef __AXK_CONTEXT_SUMMARY_H
#define __AXK_CONTEXT_SUMMARY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AXK_SUMMARY_USER_NAME_LEN   64
#define AXK_SUMMARY_MAX_FACTS       8
#define AXK_SUMMARY_FACT_LEN        160
#define AXK_SUMMARY_MAX_RECENT      3
#define AXK_SUMMARY_USER_MSG_LEN    256
#define AXK_SUMMARY_ASST_PREVIEW_LEN 200

/** 单条最近交换 */
typedef struct {
    char user_msg[AXK_SUMMARY_USER_MSG_LEN];           /**< 用户消息 (截断到256字节) */
    char assistant_preview[AXK_SUMMARY_ASST_PREVIEW_LEN]; /**< assistant 回复前200字节 */
} axk_recent_exchange_t;

/** 上下文摘要 — Flash 持久化 + RAM 缓存 */
typedef struct {
    uint8_t  version;              /**< 格式版本号 (当前=1) */
    char     user_name[AXK_SUMMARY_USER_NAME_LEN]; /**< 用户名 (从身份Q&A提取) */
    uint8_t  fact_count;           /**< 已知事实数量 (0-8) */
    char     facts[AXK_SUMMARY_MAX_FACTS][AXK_SUMMARY_FACT_LEN]; /**< 事实列表 */
    uint8_t  recent_count;         /**< 最近交换数量 (0-3) */
    axk_recent_exchange_t recent[AXK_SUMMARY_MAX_RECENT]; /**< 最近3条 */
    uint32_t last_updated;         /**< RTC 时间戳 */
    uint16_t total_turns;          /**< 累计对话轮次 */
} axk_context_summary_t;

/** RAM 大小: 1 + 64 + 1 + 8×160 + 1 + 3×(256+200) + 4 + 2 = 2717 字节 */

/**
 * @brief 初始化摘要结构体 (清零)
 * @param summary 待初始化的摘要指针
 */
void axk_context_summary_init(axk_context_summary_t *summary);

/**
 * @brief 从 messages 数组蒸馏上下文摘要
 *
 * @param messages  cJSON Array (Anthropic content blocks 格式)
 * @param summary   输出的摘要结构体 (调用者分配，栈上即可)
 */
void axk_context_summarize(cJSON *messages, axk_context_summary_t *summary);

/**
 * @brief 将摘要序列化为 cJSON 对象
 * @param summary 摘要结构体
 * @return cJSON 对象 (调用者负责 cJSON_Delete)
 */
cJSON *axk_context_summary_to_json(const axk_context_summary_t *summary);

/**
 * @brief 从 cJSON 对象反序列化摘要
 * @param json    cJSON 对象
 * @param summary 输出的摘要结构体
 * @return true 成功，false 格式不匹配
 */
bool axk_context_summary_from_json(cJSON *json, axk_context_summary_t *summary);

/**
 * @brief 将摘要格式化为 system_prompt 注入块
 * @param summary  摘要结构体
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小 (建议 ≥1536 字节)
 * @return 写入的字节数 (不含 \\0)
 */
int axk_context_summary_format_for_prompt(const axk_context_summary_t *summary,
                                          char *buf, size_t buf_size);

/**
 * @brief 检查摘要是否包含有效数据
 * @param summary 摘要结构体
 * @return true 含用户数据 (user_name 非空或 facts >0 或 recent >0)
 */
bool axk_context_summary_is_valid(const axk_context_summary_t *summary);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_CONTEXT_SUMMARY_H */
