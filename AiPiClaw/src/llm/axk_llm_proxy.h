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
 * @brief initLLM agent
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_llm_proxy_init(void);

/**
 * @brief set LLM APIkey 
 * @param[in] api_key APIkey 
 * @return OKreturn 0
 */
int axk_llm_set_api_key(const char *api_key);

/**
 * @brief set LLMprovide 商
 * @param[in] provider provide 商name 
 * @return OKreturn 0
 */
int axk_llm_set_provider(const char *provider);

/**
 * @brief set LLM模型
 * @param[in] model 模型标识
 * @return OKreturn 0
 */
int axk_llm_set_model(const char *model);

/* ── Tool Use support  ───────────────────────────────────────────── */

typedef struct {
    char id[64];        /* "toolu_xxx" */
    char name[32];      /* "web_search" */
    char *input;        /* heap-allocated JSON string */
    size_t input_len;
} llm_tool_call_t;

typedef struct {
    char *text;                                  /* accumulated text blocks */
    size_t text_len;
    llm_tool_call_t calls[MIMI_MAX_TOOL_CALLS];
    int call_count;
    bool tool_use;                               /* stop_reason == "tool_use" */
} llm_response_t;

void axk_llm_response_free(llm_response_t *resp);

/**
 * @brief send带toolcall 聊天okrequest
 * @param[in] system_prompt system提示词
 * @param[in] messages cJSONmsg数组
 * @param[in] tools_json toolJSONchars 串， or NULL表示无tool
 * @param[out] resp response结构体
 * @return OKreturn 0
 */
int axk_llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_LLM_PROXY_H */
