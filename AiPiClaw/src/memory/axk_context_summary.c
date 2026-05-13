/**
 * @file axk_context_summary.c
 * @brief 上下文摘要蒸馏 — 规则提取 + JSON 序列化
 * @version 1.0
 * @date 2026-05-13
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 蒸馏流程: messages 数组 → 身份提取 → 事实提取 → 最近交换保留 → 元数据更新
 *       每个会话轮次调用一次，增量更新 summary 结构体
 */

#include "axk_context_summary.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <strings.h>
#include <time.h>

#include "cJSON.h"

/* ── internalhelpers  ─────────────────────────────────── */

/**
 * @brief 安全截断复制字符串
 * @param dst 目标缓冲区
 * @param dst_size 目标缓冲区大小
 * @param src 源字符串 (可为NULL)
 */
static void str_copy_truncated(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    if (!src || src[0] == '\0') {
        dst[0] = '\0';
        return;
    }
    size_t n = strlen(src);
    if (n >= dst_size) n = dst_size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

/**
 * @brief 从消息对象中提取文本内容（处理字符串和数组两种 content 格式）
 * @param msg   cJSON 消息对象
 * @param buf   输出缓冲区
 * @param size  缓冲区大小
 * @return 提取到的文本长度，0 表示无文本
 */
static size_t extract_text_from_msg(cJSON *msg, char *buf, size_t size)
{
    cJSON *content;
    if (!msg || !buf || size == 0) return 0;
    buf[0] = '\0';

    content = cJSON_GetObjectItem(msg, "content");
    if (!content) return 0;

    if (cJSON_IsString(content)) {
        /* 简单字符串格式 (user msg) */
        str_copy_truncated(buf, size, content->valuestring);
        return strlen(buf);
    }

    if (cJSON_IsArray(content)) {
        /* Anthropic content blocks 格式 (assistant msg) */
        cJSON *block;
        cJSON_ArrayForEach(block, content) {
            cJSON *type = cJSON_GetObjectItem(block, "type");
            cJSON *text = cJSON_GetObjectItem(block, "text");
            if (type && cJSON_IsString(type) &&
                strcmp(type->valuestring, "text") == 0 &&
                text && cJSON_IsString(text)) {
                str_copy_truncated(buf, size, text->valuestring);
                return strlen(buf);
            }
        }
    }

    return 0;
}

/**
 * @brief 检查文本是否包含身份关键词
 */
static bool has_identity_keyword(const char *text)
{
    if (!text) return false;
    const char *kws[] = {
        "你是谁", "还记得我", "我叫", "我是", "我在",
        "我叫什么", "不知道我是谁", "知道我是谁", NULL
    };
    for (int i = 0; kws[i]; i++) {
        if (strstr(text, kws[i])) return true;
    }
    return false;
}

/**
 * @brief 检查文本是否包含纠正关键词
 */
static bool has_correction_keyword(const char *text)
{
    if (!text) return false;
    const char *kws[] = {
        "不是", "不对", "纠正", "错了",
        "不对的", "搞错了", "应该是", NULL
    };
    for (int i = 0; kws[i]; i++) {
        if (strstr(text, kws[i])) return true;
    }
    return false;
}

/**
 * @brief 检查文本是否包含配置/设置关键词
 */
static bool has_config_keyword(const char *text)
{
    if (!text) return false;
    const char *kws[] = {
        "WiFi", "wifi", "SSID", "ssid", "密码",
        "设置", "配置", "GPIO", "gpio", "LED",
        "灯", "开关", "定时", NULL
    };
    for (int i = 0; kws[i]; i++) {
        if (strstr(text, kws[i])) return true;
    }
    return false;
}

/**
 * @brief 向摘要追加一条事实 (FIFO: 满8条时替换最旧的)
 */
static void summary_add_fact(axk_context_summary_t *s, const char *fact)
{
    if (!s || !fact || fact[0] == '\0') return;

    /* 去重检查 */
    for (int i = 0; i < s->fact_count; i++) {
        if (strcmp(s->facts[i], fact) == 0) return;
    }

    if (s->fact_count < AXK_SUMMARY_MAX_FACTS) {
        str_copy_truncated(s->facts[s->fact_count], AXK_SUMMARY_FACT_LEN, fact);
        s->fact_count++;
    } else {
        /* FIFO 替换最旧，movother forward */
        memmove(s->facts[0], s->facts[1],
                (AXK_SUMMARY_MAX_FACTS - 1) * AXK_SUMMARY_FACT_LEN);
        str_copy_truncated(s->facts[AXK_SUMMARY_MAX_FACTS - 1],
                          AXK_SUMMARY_FACT_LEN, fact);
    }
}

/**
 * @brief 从文本中提取用户名 (简单策略: 取 "叫 X" 或 "是 X" 后的短语)
 */
static void extract_name_from_text(const char *text, char *out, size_t out_size)
{
    if (!text || !out || out_size == 0) { out[0] = '\0'; return; }
    out[0] = '\0';

    /* 模式: "你叫xxx"/"你是xxx" */
    const char *patterns[] = { "你叫", "你是", "叫", "是", NULL };
    for (int i = 0; patterns[i]; i++) {
        const char *pos = strstr(text, patterns[i]);
        if (pos) {
            pos += strlen(patterns[i]);
            while (*pos == ' ' || *pos == '\t') pos++;
            /* 取到下一个标点或换行为止 */
            const char *end = pos;
            while (*end && *end != '！' && *end != '!' &&
                   *end != '。' && *end != '.' && *end != '，' &&
                   *end != ',' && *end != '😊' && *end != '\n' &&
                   !(*end & 0x80)) end++;
            size_t len = end - pos;
            if (len > 0 && len < out_size) {
                memcpy(out, pos, len);
                out[len] = '\0';
                /* trim trailing whitespace */
                while (len > 0 && (out[len-1] == ' ' || out[len-1] == '\t')) {
                    out[--len] = '\0';
                }
                return;
            }
        }
    }
}

/* ── public  API ──────────────────────────────────────── */

/**
 * @brief 初始化摘要结构体 (清零)
 */
void axk_context_summary_init(axk_context_summary_t *summary)
{
    if (!summary) return;
    memset(summary, 0, sizeof(*summary));
    summary->version = 1;
}

/**
 * @brief 从 messages 数组蒸馏上下文摘要 (增量更新)
 */
void axk_context_summarize(cJSON *messages, axk_context_summary_t *summary)
{
    if (!messages || !cJSON_IsArray(messages) || !summary) return;

    int count = cJSON_GetArraySize(messages);
    if (count == 0) return;

    /* 步1: 最近3轮保留 */
    summary->recent_count = 0;
    int pair_count = 0;
    for (int i = count - 1; i >= 0 && pair_count < AXK_SUMMARY_MAX_RECENT; i--) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        if (!msg) continue;
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;

        if (strcmp(role->valuestring, "assistant") == 0) {
            /* find prev user */
            cJSON *user_msg = NULL;
            for (int j = i - 1; j >= 0; j--) {
                cJSON *um = cJSON_GetArrayItem(messages, j);
                cJSON *ur = um ? cJSON_GetObjectItem(um, "role") : NULL;
                if (ur && cJSON_IsString(ur) &&
                    strcmp(ur->valuestring, "user") == 0) {
                    user_msg = um;
                    break;
                }
            }
            if (user_msg) {
                axk_recent_exchange_t *re = &summary->recent[pair_count];
                extract_text_from_msg(user_msg, re->user_msg,
                                     AXK_SUMMARY_USER_MSG_LEN);
                extract_text_from_msg(msg, re->assistant_preview,
                                     AXK_SUMMARY_ASST_PREVIEW_LEN);
                pair_count++;
            }
        }
    }
    summary->recent_count = (uint8_t)pair_count;

    /* 步2: 身份提取 (扫描全量 messages 中的身份关键词) */
    for (int i = 0; i < count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        if (!msg) continue;
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;
        if (strcmp(role->valuestring, "user") != 0) continue;

        char user_text[512];
        extract_text_from_msg(msg, user_text, sizeof(user_text));
        if (has_identity_keyword(user_text)) {
            /* 找到紧接的 assistant 消息 */
            for (int j = i + 1; j < count; j++) {
                cJSON *am = cJSON_GetArrayItem(messages, j);
                cJSON *ar = am ? cJSON_GetObjectItem(am, "role") : NULL;
                if (ar && cJSON_IsString(ar) &&
                    strcmp(ar->valuestring, "assistant") == 0) {
                    char asst_text[300];
                    extract_text_from_msg(am, asst_text, sizeof(asst_text));
                    if (summary->user_name[0] == '\0') {
                        extract_name_from_text(asst_text, summary->user_name,
                                               AXK_SUMMARY_USER_NAME_LEN);
                    }
                    /* 也作为事实记录 */
                    char fact[AXK_SUMMARY_FACT_LEN];
                    snprintf(fact, sizeof(fact), "身份: %s", asst_text);
                    summary_add_fact(summary, fact);
                    break;
                }
            }
        }
    }

    /* 步3: 事实提取 (纠正/配置) */
    for (int i = 0; i < count; i++) {
        cJSON *msg = cJSON_GetArrayItem(messages, i);
        if (!msg) continue;
        cJSON *role = cJSON_GetObjectItem(msg, "role");
        if (!role || !cJSON_IsString(role)) continue;
        if (strcmp(role->valuestring, "user") != 0) continue;

        char user_text[512];
        extract_text_from_msg(msg, user_text, sizeof(user_text));

        if (has_correction_keyword(user_text)) {
            char fact[AXK_SUMMARY_FACT_LEN];
            snprintf(fact, sizeof(fact), "用户纠正: %.140s", user_text);
            summary_add_fact(summary, fact);
        } else if (has_config_keyword(user_text)) {
            char fact[AXK_SUMMARY_FACT_LEN];
            snprintf(fact, sizeof(fact), "用户配置: %.140s", user_text);
            summary_add_fact(summary, fact);
        }
    }

    /* 步4: 元数据更新 */
    summary->total_turns++;
    summary->last_updated = (uint32_t)time(NULL);
}

/**
 * @brief 将摘要序列化为 cJSON 对象
 */
cJSON *axk_context_summary_to_json(const axk_context_summary_t *summary)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "v", summary->version);
    cJSON_AddStringToObject(root, "name", summary->user_name);

    cJSON *facts = cJSON_AddArrayToObject(root, "facts");
    for (int i = 0; i < summary->fact_count; i++) {
        cJSON_AddItemToArray(facts, cJSON_CreateString(summary->facts[i]));
    }

    cJSON *recent = cJSON_AddArrayToObject(root, "recent");
    for (int i = 0; i < summary->recent_count; i++) {
        cJSON *ex = cJSON_CreateObject();
        cJSON_AddStringToObject(ex, "u", summary->recent[i].user_msg);
        cJSON_AddStringToObject(ex, "a", summary->recent[i].assistant_preview);
        cJSON_AddItemToArray(recent, ex);
    }

    cJSON_AddNumberToObject(root, "turns", summary->total_turns);
    cJSON_AddNumberToObject(root, "ts", (double)summary->last_updated);

    return root;
}

/**
 * @brief 从 cJSON 对象反序列化摘要
 */
bool axk_context_summary_from_json(cJSON *json, axk_context_summary_t *summary)
{
    cJSON *v, *name, *facts, *recent, *turns, *ts;
    if (!json || !summary) return false;

    v = cJSON_GetObjectItem(json, "v");
    if (!v || !cJSON_IsNumber(v) || v->valueint != 1) return false;

    axk_context_summary_init(summary);
    summary->version = (uint8_t)v->valueint;

    name = cJSON_GetObjectItem(json, "name");
    if (name && cJSON_IsString(name)) {
        str_copy_truncated(summary->user_name, AXK_SUMMARY_USER_NAME_LEN,
                          name->valuestring);
    }

    facts = cJSON_GetObjectItem(json, "facts");
    if (facts && cJSON_IsArray(facts)) {
        int fc = cJSON_GetArraySize(facts);
        if (fc > AXK_SUMMARY_MAX_FACTS) fc = AXK_SUMMARY_MAX_FACTS;
        for (int i = 0; i < fc; i++) {
            cJSON *f = cJSON_GetArrayItem(facts, i);
            if (f && cJSON_IsString(f)) {
                str_copy_truncated(summary->facts[i], AXK_SUMMARY_FACT_LEN,
                                  f->valuestring);
                summary->fact_count++;
            }
        }
    }

    recent = cJSON_GetObjectItem(json, "recent");
    if (recent && cJSON_IsArray(recent)) {
        int rc = cJSON_GetArraySize(recent);
        if (rc > AXK_SUMMARY_MAX_RECENT) rc = AXK_SUMMARY_MAX_RECENT;
        for (int i = 0; i < rc; i++) {
            cJSON *ex = cJSON_GetArrayItem(recent, i);
            if (!ex) continue;
            cJSON *u = cJSON_GetObjectItem(ex, "u");
            cJSON *a = cJSON_GetObjectItem(ex, "a");
            if (u && cJSON_IsString(u)) {
                str_copy_truncated(summary->recent[i].user_msg,
                                  AXK_SUMMARY_USER_MSG_LEN, u->valuestring);
            }
            if (a && cJSON_IsString(a)) {
                str_copy_truncated(summary->recent[i].assistant_preview,
                                  AXK_SUMMARY_ASST_PREVIEW_LEN, a->valuestring);
            }
            summary->recent_count++;
        }
    }

    turns = cJSON_GetObjectItem(json, "turns");
    if (turns && cJSON_IsNumber(turns)) {
        summary->total_turns = (uint16_t)turns->valueint;
    }

    ts = cJSON_GetObjectItem(json, "ts");
    if (ts && cJSON_IsNumber(ts)) {
        summary->last_updated = (uint32_t)ts->valuedouble;
    }

    return true;
}

/**
 * @brief 将摘要格式化为 system_prompt 注入块
 */
int axk_context_summary_format_for_prompt(const axk_context_summary_t *summary,
                                          char *buf, size_t buf_size)
{
    int pos = 0;
    if (!summary || !buf || buf_size == 0) return 0;
    buf[0] = '\0';

    /* 无内容不注入 */
    if (!axk_context_summary_is_valid(summary)) return 0;

    pos += snprintf(buf + pos, buf_size - pos, "[Context Memory]\n");

    if (summary->user_name[0]) {
        pos += snprintf(buf + pos, buf_size - pos, "User: %s\n", summary->user_name);
    }

    if (summary->fact_count > 0) {
        pos += snprintf(buf + pos, buf_size - pos, "Known:\n");
        for (int i = 0; i < summary->fact_count; i++) {
            char trimmed[AXK_SUMMARY_FACT_LEN];
            str_copy_truncated(trimmed, sizeof(trimmed), summary->facts[i]);
            /* 去除尾换行 */
            size_t tl = strlen(trimmed);
            while (tl > 0 && (trimmed[tl-1] == '\n' || trimmed[tl-1] == '\r'))
                trimmed[--tl] = '\0';
            pos += snprintf(buf + pos, buf_size - pos, "  - %s\n", trimmed);
        }
    }

    if (summary->recent_count > 0) {
        pos += snprintf(buf + pos, buf_size - pos, "Recent:\n");
        for (int i = 0; i < summary->recent_count; i++) {
            char u[AXK_SUMMARY_USER_MSG_LEN];
            char a[AXK_SUMMARY_ASST_PREVIEW_LEN];
            str_copy_truncated(u, sizeof(u), summary->recent[i].user_msg);
            str_copy_truncated(a, sizeof(a), summary->recent[i].assistant_preview);
            pos += snprintf(buf + pos, buf_size - pos,
                          "  Q: %s\n  A: %s\n", u, a);
        }
    }

    return pos;
}

/**
 * @brief 检查摘要是否包含有效数据
 */
bool axk_context_summary_is_valid(const axk_context_summary_t *summary)
{
    if (!summary) return false;
    return (summary->user_name[0] != '\0' ||
            summary->fact_count > 0 ||
            summary->recent_count > 0);
}
