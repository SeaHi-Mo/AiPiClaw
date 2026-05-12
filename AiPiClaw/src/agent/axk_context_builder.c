/**
 * @file axk_context_builder.c
 * @brief 上下文build 器实现 - build  LLM request上下文
 * @version 1.0
 * @date 2026-04-23
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_context_builder.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"
#include "mimi_config.h"

#define AXK_CTX_MAX_LEN  4096

static char s_system_prompt[AXK_CTX_MAX_LEN];

/**
 * @brief TODO: 描述axk_context_builder_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_context_builder_init(void)
{
    snprintf(s_system_prompt, sizeof(s_system_prompt),
             "You are MimiClaw, an AI assistant running on BL618 hardware by 安信可科技. "
             "You can use tools to search the web, read/write files, control GPIO, and manage schedules. "
             "Always respond in the same language as the user's query.");

    AXK_LOG_INFO("[axk_context_builder] 上下文build 器initok\r\n");
    return 0;
}

/**
 * @brief get system提示词
 *
 * @return system提示词chars 串ptr 
 */
const char *axk_context_builder_get_system_prompt(void)
{
    return s_system_prompt;
}

/**
 * @brief build full requestJSON
 *
 * @param[in] user_message usermsg
 * @param[in] tools_json toolJSONchars 串
 * @param[in] session_context 会话上下文
 * @return requestJSONchars 串，use ok后需release 
 */
char *axk_context_builder_build_request(const char *user_message,
                                         const char *tools_json,
                                         const char *session_context)
{
    cJSON *root;
    cJSON *messages;
    cJSON *msg;
    cJSON *tools;
    char *result;

    if (!user_message) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "model", MIMI_LLM_DEFAULT_MODEL);

    /* messages 数组 */
    messages = cJSON_CreateArray();

    /* system msg */
    msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "system");
    cJSON_AddStringToObject(msg, "content", s_system_prompt);
    cJSON_AddItemToArray(messages, msg);

    /* 会话上下文 */
    if (session_context && session_context[0] != '\0') {
        msg = cJSON_CreateObject();
        cJSON_AddStringToObject(msg, "role", "assistant");
        cJSON_AddStringToObject(msg, "content", session_context);
        cJSON_AddItemToArray(messages, msg);
    }

    /* usermsg */
    msg = cJSON_CreateObject();
    cJSON_AddStringToObject(msg, "role", "user");
    cJSON_AddStringToObject(msg, "content", user_message);
    cJSON_AddItemToArray(messages, msg);

    cJSON_AddItemToObject(root, "messages", messages);

    /* tools */
    if (tools_json && tools_json[0] != '\0') {
        tools = cJSON_Parse(tools_json);
        if (tools) {
            cJSON_AddItemToObject(root, "tools", tools);
        }
    }

    cJSON_AddNumberToObject(root, "max_tokens", 2048);

    result = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    return result;
}
