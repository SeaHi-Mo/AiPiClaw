/**
 * @file axk_llm_proxy.c
 * @brief LLM agent (Claude/OpenAI/DeepSeek/MiniMax) - 安信可科技 BL618 port
 * @note 整合自官方solution/mimiclaw/port
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_llm_proxy.h"
#include "axk_storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "https_client.h"

#include "axk_platform.h"
#include "axk_mimiclaw_port.h"

static const char *TAG = "llm";

#define MIMICLAW_KV_LLM_API_KEY   "mimiclaw.llm.api_key"
#define MIMICLAW_KV_LLM_MODEL     "mimiclaw.llm.model"
#define MIMICLAW_KV_LLM_PROVIDER  "mimiclaw.llm.provider"

#define LLM_API_KEY_MAX_LEN 320
#define LLM_MODEL_MAX_LEN   64
#define LLM_PROVIDER_MAX_LEN 16
#define LLM_HTTP_TIMEOUT_MS (120 * 1000)
#define LLM_RESP_INIT_CAP   (4 * 1024)
#define LLM_MAX_TOKENS_SAFE 1024
#define OPENAI_FALLBACK_MODEL "gpt-4o-mini"
#define DEEPSEEK_FALLBACK_MODEL "deepseek-chat"
#define MINIMAX_FALLBACK_MODEL "MiniMax-M2.5"
#ifndef MIMI_DEEPSEEK_API_URL
#define MIMI_DEEPSEEK_API_URL "https://api.deepseek.com/chat/completions"
#endif
#ifndef MIMI_MINIMAX_API_URL
#define MIMI_MINIMAX_API_URL "https://api.minimaxi.com/v1/chat/completions"
#endif

static char s_api_key[LLM_API_KEY_MAX_LEN] = { 0 };
static char s_model[LLM_MODEL_MAX_LEN] = MIMI_LLM_DEFAULT_MODEL;
static char s_provider[LLM_PROVIDER_MAX_LEN] = MIMI_LLM_PROVIDER_DEFAULT;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
    int status_code;
    bool oom;
} llm_resp_buf_t;

static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    size_t n;

    if (!dst || dst_size == 0) {
        return;
    }

    if (!src) {
        dst[0] = '\0';
        return;
    }

    n = strnlen(src, dst_size - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static bool provider_is_openai(void)
{
    return strcmp(s_provider, "openai") == 0;
}

static bool provider_is_deepseek(void)
{
    return strcmp(s_provider, "deepseek") == 0;
}

static bool provider_is_minimax(void)
{
    return strcmp(s_provider, "minimax") == 0;
}

static bool provider_uses_openai_format(void)
{
    return provider_is_openai() || provider_is_deepseek() || provider_is_minimax();
}

static const char *llm_api_url(void)
{
    if (provider_is_openai()) {
        return MIMI_OPENAI_API_URL;
    }
    if (provider_is_deepseek()) {
        return MIMI_DEEPSEEK_API_URL;
    }
    if (provider_is_minimax()) {
        return MIMI_MINIMAX_API_URL;
    }
    return MIMI_LLM_API_URL;
}

static bool normalize_model_for_provider(void)
{
    if (provider_is_openai()) {
        if (strncmp(s_model, "claude", 6) == 0 ||
            strncmp(s_model, "deepseek-", 9) == 0 ||
            strncmp(s_model, "MiniMax-", 8) == 0 ||
            s_model[0] == '\0') {
            safe_copy(s_model, sizeof(s_model), OPENAI_FALLBACK_MODEL);
            return true;
        }
    } else if (provider_is_deepseek()) {
        if (strncmp(s_model, "claude", 6) == 0 ||
            strncmp(s_model, "gpt-", 4) == 0 ||
            strncmp(s_model, "MiniMax-", 8) == 0 ||
            s_model[0] == '\0') {
            safe_copy(s_model, sizeof(s_model), DEEPSEEK_FALLBACK_MODEL);
            return true;
        }
    } else if (provider_is_minimax()) {
        if (strncmp(s_model, "claude", 6) == 0 ||
            strncmp(s_model, "gpt-", 4) == 0 ||
            strncmp(s_model, "deepseek-", 9) == 0 ||
            s_model[0] == '\0') {
            safe_copy(s_model, sizeof(s_model), MINIMAX_FALLBACK_MODEL);
            return true;
        }
    } else {
        if (strncmp(s_model, "gpt-", 4) == 0 ||
            strncmp(s_model, "deepseek-", 9) == 0 ||
            strncmp(s_model, "MiniMax-", 8) == 0 ||
            s_model[0] == '\0') {
            safe_copy(s_model, sizeof(s_model), MIMI_LLM_DEFAULT_MODEL);
            return true;
        }
    }

    return false;
}

static int llm_resp_buf_append(llm_resp_buf_t *rb, const uint8_t *data, size_t len)
{
    if (!rb || !data || len == 0) {
        return 0;
    }

    while (rb->len + len + 1 > rb->cap) {
        size_t new_cap = rb->cap * 2;
        char *tmp = (char *)realloc(rb->data, new_cap);
        if (!tmp) {
            rb->oom = true;
            return -1;
        }
        rb->data = tmp;
        rb->cap = new_cap;
    }

    memcpy(rb->data + rb->len, data, len);
    rb->len += len;
    rb->data[rb->len] = '\0';
    return 0;
}

static void llm_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    llm_resp_buf_t *rb = (llm_resp_buf_t *)user_data;
    (void)final_data;

    if (!rb || !rsp) {
        return;
    }
    if (rb->oom) {
        return;
    }

    if (rb->status_code == 0 && rsp->http_status_code > 0) {
        rb->status_code = rsp->http_status_code;
    }

    if (rsp->body_frag_start && rsp->body_frag_len > 0) {
        (void)llm_resp_buf_append(rb, rsp->body_frag_start, rsp->body_frag_len);
    }
}

static int llm_http_call(const char *post_data, llm_resp_buf_t *rb)
{
    char auth_header[LLM_API_KEY_MAX_LEN + 32];
    char key_header[LLM_API_KEY_MAX_LEN + 32];
    const char *headers_openai[] = {
        "Accept: application/json\r\n",
        auth_header,
        "Connection: close\r\n",
        NULL
    };
    const char *headers_anthropic[] = {
        "Accept: application/json\r\n",
        key_header,
        "anthropic-version: " MIMI_LLM_API_VERSION "\r\n",
        "Connection: close\r\n",
        NULL
    };
    struct https_client_request req = { 0 };
    const char **headers = headers_anthropic;
    int ret;

    if (provider_uses_openai_format()) {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", s_api_key);
        headers = headers_openai;
    } else {
        snprintf(key_header, sizeof(key_header), "x-api-key: %s\r\n", s_api_key);
        headers = headers_anthropic;
    }

    req.method = HTTP_POST;
    req.url = llm_api_url();
    req.protocol = "HTTP/1.1";
    req.response = llm_http_response_cb;
    req.header_fields = headers;
    req.content_type_value = "application/json";
    req.payload = post_data;
    req.payload_len = strlen(post_data);
    req.buffer_size = 2048;

    ret = https_client_request(&req, LLM_HTTP_TIMEOUT_MS, rb);
    if (ret < 0) {
        return -1;
    }

    return 0;
}

static cJSON *convert_tools_openai(const char *tools_json)
{
    cJSON *arr;
    cJSON *out;
    cJSON *tool;

    if (!tools_json) {
        return NULL;
    }

    arr = cJSON_Parse(tools_json);
    if (!arr || !cJSON_IsArray(arr)) {
        cJSON_Delete(arr);
        return NULL;
    }

    out = cJSON_CreateArray();
    cJSON_ArrayForEach(tool, arr)
    {
        cJSON *name = cJSON_GetObjectItem(tool, "name");
        cJSON *desc = cJSON_GetObjectItem(tool, "description");
        cJSON *schema = cJSON_GetObjectItem(tool, "input_schema");
        cJSON *func;
        cJSON *wrap;

        if (!name || !cJSON_IsString(name)) {
            continue;
        }

        func = cJSON_CreateObject();
        cJSON_AddStringToObject(func, "name", name->valuestring);
        if (desc && cJSON_IsString(desc)) {
            cJSON_AddStringToObject(func, "description", desc->valuestring);
        }
        if (schema) {
            cJSON_AddItemToObject(func, "parameters", cJSON_Duplicate(schema, 1));
        }

        wrap = cJSON_CreateObject();
        cJSON_AddStringToObject(wrap, "type", "function");
        cJSON_AddItemToObject(wrap, "function", func);
        cJSON_AddItemToArray(out, wrap);
    }

    cJSON_Delete(arr);
    return out;
}

static cJSON *convert_messages_openai(const char *system_prompt, cJSON *messages)
{
    cJSON *out = cJSON_CreateArray();
    cJSON *msg;

    if (system_prompt && system_prompt[0]) {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(out, sys);
    }

    if (!messages || !cJSON_IsArray(messages)) {
        return out;
    }

    cJSON_ArrayForEach(msg, messages)
    {
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        cJSON *content = cJSON_GetObjectItem(msg, "content");

        if (!role || !cJSON_IsString(role)) {
            continue;
        }

        if (content && cJSON_IsString(content)) {
            cJSON *m = cJSON_CreateObject();
            cJSON_AddStringToObject(m, "role", role->valuestring);
            cJSON_AddStringToObject(m, "content", content->valuestring);
            cJSON_AddItemToArray(out, m);
            continue;
        }

        if (!content || !cJSON_IsArray(content)) {
            continue;
        }

        if (strcmp(role->valuestring, "assistant") == 0) {
            cJSON *m = cJSON_CreateObject();
            cJSON *block;
            cJSON *tool_calls = NULL;
            char *text_buf = NULL;
            size_t off = 0;

            cJSON_AddStringToObject(m, "role", "assistant");

            cJSON_ArrayForEach(block, content)
            {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        size_t tlen = strlen(text->valuestring);
                        char *tmp = (char *)realloc(text_buf, off + tlen + 1);
                        if (!tmp) {
                            free(text_buf);
                            text_buf = NULL;
                            continue;
                        }
                        text_buf = tmp;
                        memcpy(text_buf + off, text->valuestring, tlen);
                        off += tlen;
                        text_buf[off] = '\0';
                    }
                } else if (btype && cJSON_IsString(btype) &&
                           strcmp(btype->valuestring, "tool_use") == 0) {
                    cJSON *id = cJSON_GetObjectItem(block, "id");
                    cJSON *name = cJSON_GetObjectItem(block, "name");
                    cJSON *input = cJSON_GetObjectItem(block, "input");
                    cJSON *tc;
                    cJSON *func;

                    if (!name || !cJSON_IsString(name)) {
                        continue;
                    }

                    if (!tool_calls) {
                        tool_calls = cJSON_CreateArray();
                    }

                    tc = cJSON_CreateObject();
                    if (id && cJSON_IsString(id)) {
                        cJSON_AddStringToObject(tc, "id", id->valuestring);
                    }
                    cJSON_AddStringToObject(tc, "type", "function");
                    func = cJSON_CreateObject();
                    cJSON_AddStringToObject(func, "name", name->valuestring);
                    if (input) {
                        char *args = cJSON_PrintUnformatted(input);
                        if (args) {
                            cJSON_AddStringToObject(func, "arguments", args);
                            free(args);
                        }
                    }
                    cJSON_AddItemToObject(tc, "function", func);
                    cJSON_AddItemToArray(tool_calls, tc);
                }
            }

            cJSON_AddStringToObject(m, "content", text_buf ? text_buf : "");
            if (tool_calls) {
                cJSON_AddItemToObject(m, "tool_calls", tool_calls);
            }
            cJSON_AddItemToArray(out, m);
            free(text_buf);
        } else if (strcmp(role->valuestring, "user") == 0) {
            cJSON *block;
            bool has_user_text = false;
            char *text_buf = NULL;
            size_t off = 0;

            cJSON_ArrayForEach(block, content)
            {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) &&
                    strcmp(btype->valuestring, "tool_result") == 0) {
                    cJSON *tool_id = cJSON_GetObjectItem(block, "tool_use_id");
                    cJSON *tcontent = cJSON_GetObjectItem(block, "content");
                    cJSON *tm;

                    if (!tool_id || !cJSON_IsString(tool_id)) {
                        continue;
                    }

                    tm = cJSON_CreateObject();
                    cJSON_AddStringToObject(tm, "role", "tool");
                    cJSON_AddStringToObject(tm, "tool_call_id", tool_id->valuestring);
                    cJSON_AddStringToObject(tm, "content",
                                            (tcontent && cJSON_IsString(tcontent)) ?
                                                tcontent->valuestring :
                                                "");
                    cJSON_AddItemToArray(out, tm);
                } else if (btype && cJSON_IsString(btype) &&
                           strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        size_t tlen = strlen(text->valuestring);
                        char *tmp = (char *)realloc(text_buf, off + tlen + 1);
                        if (!tmp) {
                            free(text_buf);
                            text_buf = NULL;
                            continue;
                        }
                        text_buf = tmp;
                        memcpy(text_buf + off, text->valuestring, tlen);
                        off += tlen;
                        text_buf[off] = '\0';
                        has_user_text = true;
                    }
                }
            }

            if (has_user_text) {
                cJSON *um = cJSON_CreateObject();
                cJSON_AddStringToObject(um, "role", "user");
                cJSON_AddStringToObject(um, "content", text_buf);
                cJSON_AddItemToArray(out, um);
            }

            free(text_buf);
        }
    }

    return out;
}

void axk_llm_response_free(llm_response_t *resp)
{
    int i;

    if (!resp) {
        return;
    }

    free(resp->text);
    resp->text = NULL;
    resp->text_len = 0;
    for (i = 0; i < resp->call_count; i++) {
        free(resp->calls[i].input);
        resp->calls[i].input = NULL;
    }
    resp->call_count = 0;
    resp->tool_use = false;
}

int axk_llm_chat_tools(const char *system_prompt,
                         cJSON *messages,
                         const char *tools_json,
                         llm_response_t *resp)
{
    cJSON *body;
    char *post_data;
    llm_resp_buf_t rb = { 0 };
    int err;
    cJSON *root;
    int max_tokens = MIMI_LLM_MAX_TOKENS;

    if (!resp) {
        return -1;
    }
    memset(resp, 0, sizeof(*resp));

    if (s_api_key[0] == '\0') {
        return -2;
    }

    if (max_tokens > LLM_MAX_TOKENS_SAFE) {
        max_tokens = LLM_MAX_TOKENS_SAFE;
    }

    body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "model", s_model);
    if (provider_uses_openai_format()) {
        cJSON_AddNumberToObject(body, "max_completion_tokens", max_tokens);
    } else {
        cJSON_AddNumberToObject(body, "max_tokens", max_tokens);
    }

    if (provider_uses_openai_format()) {
        cJSON *openai_msgs = convert_messages_openai(system_prompt, messages);
        cJSON_AddItemToObject(body, "messages", openai_msgs);
        if (tools_json) {
            cJSON *tools = convert_tools_openai(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
                cJSON_AddStringToObject(body, "tool_choice", "auto");
            }
        }
    } else {
        cJSON *msg_copy = cJSON_Duplicate(messages, 1);
        cJSON_AddStringToObject(body, "system", system_prompt ? system_prompt : "");
        cJSON_AddItemToObject(body, "messages", msg_copy ? msg_copy : cJSON_CreateArray());
        if (tools_json) {
            cJSON *tools = cJSON_Parse(tools_json);
            if (tools) {
                cJSON_AddItemToObject(body, "tools", tools);
            }
        }
    }

    post_data = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    if (!post_data) {
        return -1;
    }

    rb.cap = LLM_RESP_INIT_CAP;
    rb.data = (char *)calloc(1, rb.cap);
    if (!rb.data) {
        free(post_data);
        return -1;
    }

    AXK_LOG_INFO("info", "Calling LLM provider=%s model=%s body=%u bytes",
             s_provider, s_model, (unsigned int)strlen(post_data));

    err = llm_http_call(post_data, &rb);
    free(post_data);
    if (err != 0) {
        AXK_LOG_ERROR("err", "LLM HTTP failed: %s", "%d", (int)(err));
        free(rb.data);
        return err;
    }
    if (rb.oom) {
        AXK_LOG_ERROR("err", "LLM response buffer OOM while receiving");
        free(rb.data);
        return -1;
    }

    if (rb.status_code != 200) {
        AXK_LOG_ERROR("err", "LLM HTTP status=%d body=%.200s", rb.status_code, rb.data);
        free(rb.data);
        return -1;
    }

    root = cJSON_Parse(rb.data);
    free(rb.data);
    if (!root) {
        AXK_LOG_ERROR("err", "Failed to parse LLM response JSON");
        return -1;
    }

    if (provider_uses_openai_format()) {
        cJSON *choices = cJSON_GetObjectItem(root, "choices");
        cJSON *choice0 = choices && cJSON_IsArray(choices) ? cJSON_GetArrayItem(choices, 0) : NULL;
        if (choice0) {
            cJSON *finish = cJSON_GetObjectItem(choice0, "finish_reason");
            cJSON *message = cJSON_GetObjectItem(choice0, "message");
            if (finish && cJSON_IsString(finish)) {
                resp->tool_use = (strcmp(finish->valuestring, "tool_calls") == 0);
            }

            if (message) {
                cJSON *content = cJSON_GetObjectItem(message, "content");
                if (content && cJSON_IsString(content)) {
                    size_t tlen = strlen(content->valuestring);
                    resp->text = (char *)calloc(1, tlen + 1);
                    if (resp->text) {
                        memcpy(resp->text, content->valuestring, tlen);
                        resp->text_len = tlen;
                    }
                }

                cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
                if (tool_calls && cJSON_IsArray(tool_calls)) {
                    cJSON *tc;
                    cJSON_ArrayForEach(tc, tool_calls)
                    {
                        llm_tool_call_t *call;
                        cJSON *id;
                        cJSON *func;

                        if (resp->call_count >= MIMI_MAX_TOOL_CALLS) {
                            break;
                        }

                        call = &resp->calls[resp->call_count];
                        id = cJSON_GetObjectItem(tc, "id");
                        func = cJSON_GetObjectItem(tc, "function");
                        if (id && cJSON_IsString(id)) {
                            strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                        }
                        if (func) {
                            cJSON *name = cJSON_GetObjectItem(func, "name");
                            cJSON *args = cJSON_GetObjectItem(func, "arguments");
                            if (name && cJSON_IsString(name)) {
                                strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                            }
                            if (args && cJSON_IsString(args)) {
                                call->input = strdup(args->valuestring);
                                if (call->input) {
                                    call->input_len = strlen(call->input);
                                }
                            }
                        }
                        resp->call_count++;
                    }
                    if (resp->call_count > 0) {
                        resp->tool_use = true;
                    }
                }
            }
        }
    } else {
        cJSON *stop_reason = cJSON_GetObjectItem(root, "stop_reason");
        cJSON *content = cJSON_GetObjectItem(root, "content");

        if (stop_reason && cJSON_IsString(stop_reason)) {
            resp->tool_use = (strcmp(stop_reason->valuestring, "tool_use") == 0);
        }

        if (content && cJSON_IsArray(content)) {
            cJSON *block;
            size_t total_text = 0;

            cJSON_ArrayForEach(block, content)
            {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                if (btype && cJSON_IsString(btype) && strcmp(btype->valuestring, "text") == 0) {
                    cJSON *text = cJSON_GetObjectItem(block, "text");
                    if (text && cJSON_IsString(text)) {
                        total_text += strlen(text->valuestring);
                    }
                }
            }

            if (total_text > 0) {
                resp->text = (char *)calloc(1, total_text + 1);
                if (resp->text) {
                    cJSON_ArrayForEach(block, content)
                    {
                        cJSON *btype = cJSON_GetObjectItem(block, "type");
                        cJSON *text;
                        size_t tlen;
                        if (!btype || !cJSON_IsString(btype) || strcmp(btype->valuestring, "text") != 0) {
                            continue;
                        }
                        text = cJSON_GetObjectItem(block, "text");
                        if (!text || !cJSON_IsString(text)) {
                            continue;
                        }
                        tlen = strlen(text->valuestring);
                        memcpy(resp->text + resp->text_len, text->valuestring, tlen);
                        resp->text_len += tlen;
                    }
                    resp->text[resp->text_len] = '\0';
                }
            }

            cJSON_ArrayForEach(block, content)
            {
                cJSON *btype = cJSON_GetObjectItem(block, "type");
                llm_tool_call_t *call;

                if (!btype || !cJSON_IsString(btype) || strcmp(btype->valuestring, "tool_use") != 0) {
                    continue;
                }
                if (resp->call_count >= MIMI_MAX_TOOL_CALLS) {
                    break;
                }

                call = &resp->calls[resp->call_count];
                cJSON *id = cJSON_GetObjectItem(block, "id");
                cJSON *name = cJSON_GetObjectItem(block, "name");
                cJSON *input = cJSON_GetObjectItem(block, "input");
                if (id && cJSON_IsString(id)) {
                    strncpy(call->id, id->valuestring, sizeof(call->id) - 1);
                }
                if (name && cJSON_IsString(name)) {
                    strncpy(call->name, name->valuestring, sizeof(call->name) - 1);
                }
                if (input) {
                    call->input = cJSON_PrintUnformatted(input);
                    if (call->input) {
                        call->input_len = strlen(call->input);
                    }
                }
                resp->call_count++;
            }
        }
    }

    cJSON_Delete(root);
    AXK_LOG_INFO("info", "LLM response text=%u tool_calls=%d stop=%s",
             (unsigned int)resp->text_len,
             resp->call_count,
             resp->tool_use ? "tool_use" : "end_turn");
    return 0;
}

int axk_llm_proxy_init(void)
{
    size_t out_len = 0;
    char kv_api_key[LLM_API_KEY_MAX_LEN] = { 0 };
    char kv_model[LLM_MODEL_MAX_LEN] = { 0 };
    char kv_provider[LLM_PROVIDER_MAX_LEN] = { 0 };

    if (MIMI_SECRET_API_KEY[0] != '\0') {
        safe_copy(s_api_key, sizeof(s_api_key), MIMI_SECRET_API_KEY);
    }
    if (MIMI_SECRET_MODEL[0] != '\0') {
        safe_copy(s_model, sizeof(s_model), MIMI_SECRET_MODEL);
    }
    if (MIMI_SECRET_MODEL_PROVIDER[0] != '\0') {
        safe_copy(s_provider, sizeof(s_provider), MIMI_SECRET_MODEL_PROVIDER);
    }

    if (axk_kv_get_blob(MIMICLAW_KV_LLM_API_KEY, kv_api_key, sizeof(kv_api_key), &out_len) == 0 &&
        out_len > 1) {
        safe_copy(s_api_key, sizeof(s_api_key), kv_api_key);
    }
    if (axk_kv_get_blob(MIMICLAW_KV_LLM_MODEL, kv_model, sizeof(kv_model), &out_len) == 0 &&
        out_len > 1) {
        safe_copy(s_model, sizeof(s_model), kv_model);
    }
    if (axk_kv_get_blob(MIMICLAW_KV_LLM_PROVIDER, kv_provider, sizeof(kv_provider), &out_len) == 0 &&
        out_len > 1) {
        safe_copy(s_provider, sizeof(s_provider), kv_provider);
    }
    if (normalize_model_for_provider()) {
        (void)axk_kv_set_blob(MIMICLAW_KV_LLM_MODEL, s_model, strlen(s_model) + 1);
    }

    AXK_LOG_INFO("info", "LLM init provider=%s model=%s key=%s",
             s_provider,
             s_model,
             s_api_key[0] ? "configured" : "missing");
    return 0;
}

int axk_llm_set_api_key(const char *api_key)
{
    size_t len;
    if (!api_key || api_key[0] == '\0') {
        return -1;
    }
    len = strlen(api_key);
    if (len >= sizeof(s_api_key)) {
        return -1;
    }
    if (axk_kv_set_blob(MIMICLAW_KV_LLM_API_KEY, api_key, len + 1) != 0) {
        return -1;
    }
    safe_copy(s_api_key, sizeof(s_api_key), api_key);
    return 0;
}

int axk_llm_set_model(const char *model)
{
    size_t len;
    if (!model || model[0] == '\0') {
        return -1;
    }
    len = strlen(model);
    if (len >= sizeof(s_model)) {
        return -1;
    }
    if (axk_kv_set_blob(MIMICLAW_KV_LLM_MODEL, model, len + 1) != 0) {
        return -1;
    }
    safe_copy(s_model, sizeof(s_model), model);
    if (normalize_model_for_provider()) {
        (void)axk_kv_set_blob(MIMICLAW_KV_LLM_MODEL, s_model, strlen(s_model) + 1);
    }
    return 0;
}

int axk_llm_set_provider(const char *provider)
{
    size_t len;
    if (!provider || provider[0] == '\0') {
        return -1;
    }
    len = strlen(provider);
    if (len >= sizeof(s_provider)) {
        return -1;
    }
    if (axk_kv_set_blob(MIMICLAW_KV_LLM_PROVIDER, provider, len + 1) != 0) {
        return -1;
    }
    safe_copy(s_provider, sizeof(s_provider), provider);
    if (normalize_model_for_provider()) {
        (void)axk_kv_set_blob(MIMICLAW_KV_LLM_MODEL, s_model, strlen(s_model) + 1);
    }
    return 0;
}
