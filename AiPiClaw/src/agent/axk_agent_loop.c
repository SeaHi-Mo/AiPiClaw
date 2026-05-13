/**
 * @file axk_agent_loop.c
 * @brief agentmain loop - 安信可科技 BL618 port
 *
 * @note 整合自官方solution/mimiclaw/port
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_agent_loop.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "bflb_rtc.h"
#include "axk_message_bus.h"
#include "axk_llm_proxy.h"
#include "axk_tool_registry.h"
#include "axk_skill_loader.h"
#include "axk_serial_cli.h"

#include "cJSON.h"
#include "axk_platform.h"
#include "axk_session_mgr.h"
#include "axk_context_summary.h"
#include "FreeRTOS.h"
#include "task.h"

static const char *TAG __attribute__((unused)) = "agent";

#define TOOL_OUTPUT_SIZE  (8 * 1024)
#define SYSTEM_PROMPT_SIZE 1024

static TaskHandle_t s_agent_task = NULL;

/**
 * @brief 从文本中移除 &lt;think&gt;...&lt;/think&gt; 思考标签，原地修改字符串
 * @param[in,out] text 待处理的字符串（会被原地修改，仅移除标签内容）
 * @return 处理后的字符串指针（同 text 入参）
 */
static char *strip_think_tags(char *text)
{
    char *open, *close;

    if (!text) return text;

    open = strstr(text, "<think>");
    while (open != NULL) {
        close = strstr(open, "</think>");
        if (close == NULL) {
            /* 有开头无结尾，抹掉从 open 到末尾 */
            *open = '\0';
            break;
        }
        close += 8; /* skip "</think>" */
        /* 将 close 之后的内容移到 open 位置 */
        memmove(open, close, strlen(close) + 1);
        open = strstr(text, "<think>");
    }

    return text;
}

/**
 * @brief 移除字符串开头和结尾的空白字符（空格、\\t、\\r、\\n），原地修改
 * @param[in,out] str 待处理的字符串
 * @return 处理后的字符串指针（同入参，或NULL入参返回NULL）
 */
static char *trim_whitespace(char *str)
{
    char *end;

    if (!str) return NULL;

    /* 跳过开头空白 */
    while (*str == ' ' || *str == '\t' || *str == '\r' || *str == '\n') {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    /* 跳过结尾空白 */
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        end--;
    }

    *(end + 1) = '\0';

    return str;
}

/**
 * @brief 不区分大小写检查haystack中是否包含needle子字符串
 * @param needle 要查找的子字符串
 * @return 找到返回true，未找到或参数无效返回false
 */
static bool str_contains_nocase(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle || needle[0] == '\0') {
        return false;
    }

    needle_len = strlen(needle);
    while (*haystack != '\0') {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return true;
        }
        haystack++;
    }

    return false;
}

/**
 * @brief 检查文本是否包含实时/当前时间锚点关键词（如today/now/最新/今天等）
 *
 * @param text 待检查的文本字符串
 * @return 包含时间锚点关键词返回true，否则返回false
 */
static bool text_has_current_time_anchor(const char *text)
{
    static const char *terms[] = {
        "today", "tonight", "now", "current", "currently", "latest",
        "recent", "recently", "breaking", "today's", "今天", "今日",
        "现在 ", "current ", "实时", "最新", "近期", "最近", NULL
    };
    int i;

    if (!text || text[0] == '\0') {
        return false;
    }

    for (i = 0; terms[i] != NULL; i++) {
        if (str_contains_nocase(text, terms[i]) || strstr(text, terms[i]) != NULL) {
            return true;
        }
    }

    return false;
}

/**
 * @brief 从文本中提取显式出现的4位年份数字（2000-2099范围）
 *
 * @param text 待解析的文本字符串
 * @return 成功返回年份值（2000-2099），未找到返回0
 */
static int extract_explicit_year(const char *text)
{
    const char *p;

    if (!text) {
        return 0;
    }

    for (p = text; p[0] && p[1] && p[2] && p[3]; p++) {
        int year;

        if (p[0] < '0' || p[0] > '9' ||
            p[1] < '0' || p[1] > '9' ||
            p[2] < '0' || p[2] > '9' ||
            p[3] < '0' || p[3] > '9') {
            continue;
        }

        year = (p[0] - '0') * 1000 +
               (p[1] - '0') * 100 +
               (p[2] - '0') * 10 +
               (p[3] - '0');
        if (year >= 2000 && year <= 2099) {
            return year;
        }
    }

    return 0;
}

/**
 * @brief 从RTC时钟获取当前年份
 *
 * @param year_out 输出参数，接收当前年份值
 * @return 成功返回true，RTC无效或参数为空返回false
 */
static bool get_current_year(int *year_out)
{
    struct bflb_tm tm_now;
    uint64_t ts = bflb_rtc_get_utc_timestamp();

    if (!year_out || ts < 1735689600) {
        return false;
    }

    bflb_rtc_get_utc_time(&tm_now);
    *year_out = tm_now.tm_year + 1900;
    return true;
}

/**
 * @brief 判断web_search查询是否应锚定回用户原始输入（当用户查询含时间锚点但工具查询缺少时触发）
 *
 * @param user_query 用户的原始查询文本
 * @param tool_query LLM生成的工具调用查询文本
 * @return 需要重写工具查询返回true，否则返回false
 */
static bool should_anchor_web_search_to_user(const char *user_query, const char *tool_query)
{
    int user_year;
    int tool_year;
    int current_year = 0;
    bool have_current_year;

    if (!text_has_current_time_anchor(user_query)) {
        return false;
    }

    if (!tool_query || tool_query[0] == '\0') {
        return true;
    }

    if (!text_has_current_time_anchor(tool_query)) {
        return true;
    }

    user_year = extract_explicit_year(user_query);
    tool_year = extract_explicit_year(tool_query);
    have_current_year = get_current_year(&current_year);

    if (user_year == 0 && tool_year != 0) {
        if (!have_current_year || tool_year != current_year) {
            return true;
        }
    }

    return false;
}

static char *rewrite_web_search_input(const char *tool_input, const char *replacement_query)
{
    cJSON *input;
    cJSON *query_item;
    char *rewritten;

    if (!replacement_query || replacement_query[0] == '\0') {
        return NULL;
    }

    input = cJSON_Parse(tool_input ? tool_input : "{}");
    if (!input) {
        return NULL;
    }

    query_item = cJSON_GetObjectItem(input, "query");
    if (query_item) {
        cJSON_SetValuestring(query_item, replacement_query);
    } else {
        cJSON_AddStringToObject(input, "query", replacement_query);
    }

    rewritten = cJSON_PrintUnformatted(input);
    cJSON_Delete(input);
    return rewritten;
}

/**
 * @brief 预处理工具调用输入参数，对web_search调用进行时间锚定改写
 *
 * @param resp LLM的tool_use响应结果
 * @param user_query 用户的原始查询文本
 * @param tool_inputs 输出数组，存储改写后的工具输入JSON字符串（调用者负责分配数组，元素由本函数malloc）
 * @return 无返回值
 */
static void prepare_tool_inputs(const llm_response_t *resp, const char *user_query, char **tool_inputs)
{
    int i;

    for (i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *input = NULL;
        cJSON *query = NULL;
        const char *tool_query = NULL;

        tool_inputs[i] = NULL;
        if (strcmp(call->name, "web_search") != 0 || !user_query || user_query[0] == '\0') {
            continue;
        }

        input = cJSON_Parse(call->input ? call->input : "{}");
        if (input) {
            query = cJSON_GetObjectItem(input, "query");
            if (query && cJSON_IsString(query)) {
                tool_query = query->valuestring;
            }
        }

        if (should_anchor_web_search_to_user(user_query, tool_query)) {
            tool_inputs[i] = rewrite_web_search_input(call->input, user_query);
            if (tool_inputs[i]) {
                AXK_LOG_INFO("agent",
                         "anchored web_search query to user text: original=%s replacement=%s",
                         tool_query ? tool_query : "(empty)",
                         user_query);
            }
        }

        cJSON_Delete(input);
    }
}

/**
 * @brief 释放prepare_tool_inputs分配的每个工具输入字符串内存
 *
 * @param resp LLM的tool_use响应结果（用于获取call_count确定数组长度）
 * @param tool_inputs 工具输入字符串数组
 * @return 无返回值
 */
static void free_tool_inputs(const llm_response_t *resp, char **tool_inputs)
{
    int i;

    for (i = 0; i < resp->call_count; i++) {
        free(tool_inputs[i]);
        tool_inputs[i] = NULL;
    }
}

/**
 * @brief 构建LLM系统提示词，包含当前日期上下文和上下文摘要
 *
 * @param buf         输出缓冲区指针
 * @param size        缓冲区最大字节数
 * @param session_id  会话标识符 (NULL=无摘要)
 * @return 无返回值
 */
static void build_system_prompt(char *buf, size_t size, const char *session_id)
{
    uint64_t now = bflb_rtc_get_utc_timestamp();
    struct bflb_tm tm_now;
    char date_line[64] = { 0 };
    char ctx_block[1536] = { 0 };

    if (now >= 1735689600) {
        bflb_rtc_get_utc_time(&tm_now);
        snprintf(date_line,
                 sizeof(date_line),
                 "Current local date context: %04d-%02d-%02d.",
                 tm_now.tm_year + 1900,
                 tm_now.tm_mon + 1,
                 tm_now.tm_mday);
    }

    /* 加载上下文摘要并格式化 */
    if (session_id) {
        axk_context_summary_t summary;
        axk_context_summary_init(&summary);
        if (axk_session_load_summary(session_id, &summary) == 0) {
            axk_context_summary_format_for_prompt(&summary, ctx_block, sizeof(ctx_block));
        }
    }

    snprintf(buf, size,
             "You are MimiClaw running on Bouffalo SDK.\n"
             "Use available tools when needed.\n"
             "If user asks for current time/date, call get_current_time.\n"
             "For time-sensitive questions about today/latest/current/recent news, prices, weather, or markets:\n"
             "- Keep the first web_search query very close to the user's original wording.\n"
             "- Do not invent or change years unless the user explicitly specifies a year.\n"
             "- If the user asks about today/latest and gives no year, prefer current-date information over historical years.\n"
             "When finished, answer clearly and concisely.\n"
             "%s\n"
             "%s\n"
             "%s",
             date_line,
             ctx_block[0] ? ctx_block : "",
             "Do NOT repeat old Q&A as prefix. Only respond to the user's latest message.");
}

static cJSON *build_assistant_content(const llm_response_t *resp, char **tool_inputs)
{
    cJSON *content = cJSON_CreateArray();
    int i;

    if (resp->text && resp->text_len > 0) {
        cJSON *text_block = cJSON_CreateObject();
        cJSON_AddStringToObject(text_block, "type", "text");
        cJSON_AddStringToObject(text_block, "text", resp->text);
        cJSON_AddItemToArray(content, text_block);
    }

    for (i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        cJSON *tool_block = cJSON_CreateObject();
        const char *tool_input = tool_inputs && tool_inputs[i] ? tool_inputs[i] : (call->input ? call->input : "{}");
        cJSON *input = cJSON_Parse(tool_input);

        cJSON_AddStringToObject(tool_block, "type", "tool_use");
        cJSON_AddStringToObject(tool_block, "id", call->id);
        cJSON_AddStringToObject(tool_block, "name", call->name);
        cJSON_AddItemToObject(tool_block, "input", input ? input : cJSON_CreateObject());
        cJSON_AddItemToArray(content, tool_block);
    }

    return content;
}

static cJSON *build_tool_results(const llm_response_t *resp,
                                 char **tool_inputs,
                                 char *tool_output,
                                 size_t tool_output_size)
{
    cJSON *content = cJSON_CreateArray();
    int i;

    for (i = 0; i < resp->call_count; i++) {
        const llm_tool_call_t *call = &resp->calls[i];
        const char *tool_input = tool_inputs && tool_inputs[i] ? tool_inputs[i] : (call->input ? call->input : "{}");
        cJSON *result_block;
        int tool_ret;

        tool_output[0] = '\0';
        tool_ret = axk_tool_registry_execute(call->name, tool_input, tool_output, tool_output_size);
        AXK_LOG_INFO("agent", "Tool %s ret=%s(%d)", call->name, (int)tool_ret, tool_ret);

        result_block = cJSON_CreateObject();
        cJSON_AddStringToObject(result_block, "type", "tool_result");
        cJSON_AddStringToObject(result_block, "tool_use_id", call->id);
        cJSON_AddStringToObject(result_block, "content", tool_output);
        cJSON_AddItemToArray(content, result_block);
    }

    return content;
}

/**
 * @brief Agent主循环FreeRTOS任务：从消息总线拉取入站消息，执行技能匹配或LLM对话（含工具调用迭代）
 *
 * @param arg 任务参数（未使用，保留兼容）
 * @return 无返回值
 */
static void agent_loop_task(void *arg)
{
    const char *tools_json;
    char system_prompt[SYSTEM_PROMPT_SIZE];
    static char tool_output[TOOL_OUTPUT_SIZE];
    (void)arg;

    /* Early printf to confirm task scheduling - must appear before any blocking call */
    printf("[agent] === task entry ===\r\n");
    printf("[agent] agent loop started\r\n");

    /* Fetch tools_json after early logging to help pinpoint any crash location */
    tools_json = axk_tool_registry_get_tools_json();
    printf("[agent] tools_json=%p\r\n", (void*)tools_json);

    /* Register as inbound consumer for Task Notification wake-up */
    axk_message_bus_set_inbound_consumer(xTaskGetCurrentTaskHandle());
    printf("[agent] registered as inbound consumer\r\n");

    while (1) {
        axk_serial_cli_poll();  /* 轮询 UART RX，检查是否有串口输入 */

        /* 每 30 分钟清理一次过期会话 */
        {
            static uint32_t s_last_cleanup = 0;
            uint32_t now = xTaskGetTickCount();
            if ((now - s_last_cleanup) > pdMS_TO_TICKS(30 * 60 * 1000)) {
                axk_session_cleanup_stale(pdMS_TO_TICKS(30 * 60 * 1000));
                s_last_cleanup = now;
            }
        }

        mimi_msg_t msg;
        cJSON *messages = NULL;
        cJSON *user_msg = NULL;
        char *final_text = NULL;
        int iteration = 0;
        bool is_error = false;
        int err = axk_message_bus_pop_inbound(&msg, UINT32_MAX);
        if (err != 0) {
            if (err == -2) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            continue;
        }

        printf("[AGENT] inbound ch=%s content=%.60s\r\n", msg.channel, msg.content ? msg.content : "(null)");

        /* skill match ：优先执行本地技能，skip  LLM call  */
        {
            const char *skill_name = axk_skill_match(msg.content);
            if (skill_name) {
                char skill_out[512];
                int skill_ret = axk_skill_execute(skill_name, msg.content, skill_out, sizeof(skill_out));
                if (skill_ret == 0) {
                    mimi_msg_t out = { 0 };
                    strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
                    strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);
                    out.content = strdup(skill_out);
                    if (out.content) {
                        out.priority = MIMI_PRIO_NORMAL;  /**< AIresponsemsg */
                        if (axk_message_bus_push_outbound(&out) != 0) {
                            AXK_LOG_WARN("agent", "drop outbound: queue full");
                        }
                        free(out.content);  /**< pushinternalstrdup副本，无论成败都release call 者副本 */
                    }
                    free(msg.content);
                    continue;   /* skip 后续 LLM process */
                }
                AXK_LOG_WARN("agent", "skill '%s' execute failed, fallback to LLM", skill_name);
            }
        }

        /* ── 构建 session_id ── */
        char session_id[AXK_SESSION_ID_LEN];
        snprintf(session_id, sizeof(session_id), "%s:%s", msg.channel, msg.chat_id);

        /* ── 从 session_mgr 恢复历史 messages ── */
        messages = axk_session_load_messages(session_id);

        /* ── 追加当前用户消息 ── */
        user_msg = cJSON_CreateObject();
        cJSON_AddStringToObject(user_msg, "role", "user");
        cJSON_AddStringToObject(user_msg, "content", msg.content ? msg.content : "");
        cJSON_AddItemToArray(messages, user_msg);

        while (iteration < MIMI_AGENT_MAX_TOOL_ITER) {
            llm_response_t resp;
            char *tool_inputs[MIMI_MAX_TOOL_CALLS] = { 0 };

            build_system_prompt(system_prompt, sizeof(system_prompt), session_id);
            err = axk_llm_chat_tools(system_prompt, messages, tools_json, &resp);
            if (err != 0) {
                AXK_LOG_ERROR("agent", "llm call failed: err=%d", (int)err);
                if (!final_text) {
                    if (err == -2) {
                        final_text = strdup("LLM调用失败: 未配置API密钥。请通过串口CLI设置 llm_key <your_key>");
                        is_error = true;
                    } else {
                        final_text = strdup("LLM调用失败，请检查网络连接或稍后重试。");
                        is_error = true;
                    }
                }
                break;
            }

            if (!resp.tool_use) {
                if (resp.text && resp.text_len > 0) {
                    strip_think_tags(resp.text);
                    final_text = strdup(resp.text);
                }
                axk_llm_response_free(&resp);
                break;
            }

            AXK_LOG_INFO("agent", "tool_use iteration=%d calls=%d", iteration + 1, resp.call_count);
            prepare_tool_inputs(&resp, msg.content, tool_inputs);

            cJSON *asst_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(asst_msg, "role", "assistant");
            cJSON_AddItemToObject(asst_msg, "content", build_assistant_content(&resp, tool_inputs));
            cJSON_AddItemToArray(messages, asst_msg);

            cJSON *result_msg = cJSON_CreateObject();
            cJSON_AddStringToObject(result_msg, "role", "user");
            cJSON_AddItemToObject(result_msg,
                                  "content",
                                  build_tool_results(&resp, tool_inputs, tool_output, sizeof(tool_output)));
            cJSON_AddItemToArray(messages, result_msg);

            free_tool_inputs(&resp, tool_inputs);
            axk_llm_response_free(&resp);
            iteration++;
        }

        /* ── 截断消息历史到 MIMI_SESSION_MAX_MSGS 条 ── */
        while (cJSON_GetArraySize(messages) > MIMI_SESSION_MAX_MSGS) {
            cJSON_DeleteItemFromArray(messages, 0);
        }

        /* ── 保存回 session_mgr ── */
        axk_session_save_messages(session_id, messages);

        /* ── 释放 messages ── */
        cJSON_Delete(messages);

        {
            mimi_msg_t out = { 0 };
            strncpy(out.channel, msg.channel, sizeof(out.channel) - 1);
            strncpy(out.chat_id, msg.chat_id, sizeof(out.chat_id) - 1);

            if (!final_text || final_text[0] == '\0') {
                /* 无LLM回复时，用最后一个工具的返回结果 */
                if (final_text) free(final_text);
                if (tool_output[0] != '\0') {
                    final_text = strdup(tool_output);
                } else {
                    final_text = strdup("Sorry, no response.");
                    is_error = true;
                }
            } else {
                trim_whitespace(final_text);
                if (final_text[0] == '\0') {
                    free(final_text);
                    if (tool_output[0] != '\0') {
                        final_text = strdup(tool_output);
                    } else {
                        final_text = strdup("Sorry, no response.");
                        is_error = true;
                    }
                }
            }

            out.content = final_text;
            out.priority = MIMI_PRIO_NORMAL;  /**< AIresponsemsg */
            out.is_error = is_error;
            size_t out_len = out.content ? strlen(out.content) : 0;
            AXK_LOG_INFO("[agent_dbg] push_outbound len=%u channel=%s\r\n",
                         (unsigned int)out_len, out.channel);
            printf("[agent_dbg] push_outbound channel=%s chat_id=%s content=%.80s\r\n",
                   out.channel, out.chat_id, out.content ? out.content : "(null)");
            fflush(stdout);
            if (!out.content) {
                AXK_LOG_WARN("agent", "drop outbound: no memory");
            } else if (axk_message_bus_push_outbound(&out) != 0) {
                AXK_LOG_WARN("agent", "drop outbound: queue full");
                free(out.content);
                final_text = NULL;  /**< pushFAIL后置empty ，避免后续double-free */
            } else {
                final_text = NULL;
            }
        }

        free(final_text);
        final_text = NULL;
        free(msg.content);
    }
}

/**
 * @brief Agent循环运行入口（API兼容保留，实际任务由axk_agent_loop_start创建独立FreeRTOS任务执行）
 *
 * @return 无返回值
 */
void axk_agent_loop_run(void)
{
    /* main loop由 axk_agent_loop_start() start为 FreeRTOS 独立task */
    /* 本func 为 API 兼容性保留，not 执行实际操作 */
}

/**
 * @brief 初始化Agent循环模块，打印初始化日志
 *
 * @return 成功返回0
 */
int axk_agent_loop_init(void)
{
    AXK_LOG_INFO("agent", "agent loop initialized");
    return 0;
}

/**
 * @brief 启动Agent循环，创建agent_loop_task FreeRTOS任务（仅首次调用有效，重复调用直接返回成功）
 *
 * @return 成功返回0，任务创建失败返回-1
 */
int axk_agent_loop_start(void)
{
    if (s_agent_task) {
        return 0;
    }

    if (xTaskCreate(agent_loop_task, "agent_loop", MIMI_AGENT_STACK, NULL, MIMI_AGENT_PRIO, &s_agent_task) != pdPASS) {
        s_agent_task = NULL;
        return -1;
    }

    return 0;
}
