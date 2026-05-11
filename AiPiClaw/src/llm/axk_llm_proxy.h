/**
 * @file axk_llm_proxy.h
 * @brief llm_proxy module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_LLM_PROXY_H
#define __AXK_LLM_PROXY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include "mimi_config.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化LLM代理模块，加载API密钥和配置
 *
 * @return 成功返回0，失败返回非零
 */
int axk_llm_proxy_init(void);

/**
 * @brief 设置LLM API密钥并持久化存储
 *
 * @param[in] api_key API密钥字符串
 * @return 成功返回0，失败返回-1
 */
int axk_llm_set_api_key(const char *api_key);

/**
 * @brief 设置LLM提供商并持久化存储，自动规范化模型名称
 *
 * @param[in] provider 提供商名称（如"openai"/"deepseek"/"minimax"等）
 * @return 成功返回0，失败返回-1
 */
int axk_llm_set_provider(const char *provider);

/**
 * @brief 设置LLM模型名称并持久化存储，自动规范化
 *
 * @param[in] model 模型标识字符串（如"claude-sonnet-4-20250514"）
 * @return 成功返回0，失败返回-1
 */
int axk_llm_set_model(const char *model);

/* ── Tool Use support  ───────────────────────────────────────────── */

typedef struct {
    char id[64];        /* 工具调用ID，如"toolu_xxx" */
    char name[32];      /* 工具名称，如"web_search" */
    char *input;        /* 堆分配的JSON参数字符串 */
    size_t input_len;   /* 输入参数JSON字符串的长度 */
} llm_tool_call_t;

typedef struct {
    char *text;                                  /* 累积的文本回复内容 */
    size_t text_len;                             /* 文本回复的长度 */
    llm_tool_call_t calls[MIMI_MAX_TOOL_CALLS];  /* 工具调用数组 */
    int call_count;                              /* 实际工具调用数量 */
    bool tool_use;                               /* 停止原因为tool_use时为true */
} llm_response_t;

/**
 * @brief 释放LLM响应结构体中动态分配的内存
 *
 * @param resp 响应结构体指针
 */
void axk_llm_response_free(llm_response_t *resp);

/**
 * @brief 发送带工具调用的LLM聊天请求，支持OpenAI/Anthropic/DeepSeek/MiniMax多后端
 *
 * @param[in] system_prompt 系统提示词
 * @param[in] messages 消息历史cJSON数组
 * @param[in] tools_json 工具定义JSON字符串，为NULL表示不使用工具
 * @param[out] resp 输出响应结构体指针
 * @return 成功返回0，失败返回负数错误码
 */
int axk_llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_LLM_PROXY_H */
