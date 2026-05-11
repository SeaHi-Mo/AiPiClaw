/**
 * @file axk_telegram_bot.c
 * @brief Telegram机器人 - 安信可科技 BL618 port
 *
 * @note 整合自官方solution/mimiclaw/port
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_telegram_bot.h"
#include "axk_storage.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#include "https_client.h"
#include "cJSON.h"
#include "axk_platform.h"
#include "task.h"

#include "mimi_config.h"
#include "axk_mimiclaw_port.h"
#include "axk_message_bus.h"

static const char *TAG = "telegram";

#define MIMICLAW_KV_TG_TOKEN   "mimiclaw.tg.token"
#define MIMICLAW_KV_TG_OFFSET  "mimiclaw.tg.offset"

#define TG_TOKEN_MAX_LEN             128
#define TG_HTTP_TIMEOUT_MS           ((MIMI_TG_POLL_TIMEOUT_S + 8) * 1000)
#define TG_HTTP_RESP_INIT_CAP        (4 * 1024)
#define TG_NO_TOKEN_BACKOFF_MS       5000
#define TG_POLL_ERROR_BACKOFF_MS     3000
#define TG_OFFSET_SAVE_STEP          10
#define TG_OFFSET_SAVE_INTERVAL_MS   (30 * 1000)
#define TG_HTTP_REQ_RETRY            2
#define TG_HTTP_REQ_RETRY_DELAY_MS   500
#define TG_POLL_FALLBACK_TIMEOUT_S   0

typedef struct {
    char *data;
    size_t len;       /**< 已接收数据长度 */
    size_t cap;       /**< 缓冲区总容量 */
    int status_code; /**< HTTP响应状态码 */
    bool oom;         /**< 内存不足标志 */
} tg_http_resp_t;

static TaskHandle_t s_poll_task;
static bool s_poll_started;
static char s_bot_token[TG_TOKEN_MAX_LEN] = MIMI_SECRET_TG_TOKEN;
static int64_t s_update_offset;
static int64_t s_last_saved_offset = -1;
static uint32_t s_last_offset_save_ms;

/**
 * @brief 调用Telegram Bot API
 *
 * @param[in] method API方法名
 * @param[in] post_data POST请求体（NULL则GET）
 * @param[out] out_body 响应体（调用者需free）
 * @param[out] out_status HTTP状态码
 * @return 0成功，-1失败
 */
static int tg_api_call(const char *method, const char *post_data, char **out_body, int *out_status);

/**
 * @brief 安全字符串拷贝（带截断保护）
 *
 * @param[out] dst 目标缓冲区
 * @param[in] dst_size 目标缓冲区大小
 * @param[in] src 源字符串
 */
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

/**
 * @brief 判断字符串是否为纯整数（含负号）
 *
 * @param[in] s 待检查字符串
 * @return true是整数，false不是
 */
static bool tg_is_integer_string(const char *s)
{
    size_t i = 0;

    if (!s || s[0] == '\0') {
        return false;
    }

    if (s[0] == '-') {
        i = 1;
        if (s[1] == '\0') {
            return false;
        }
    }

    for (; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) {
            return false;
        }
    }
    return true;
}

/**
 * @brief 去除字符串首尾空白字符（原地修改）
 *
 * @param[in,out] s 待处理的字符串
 */
static void tg_trim_spaces(char *s)
{
    char *start = s;
    char *end;

    if (!s || s[0] == '\0') {
        return;
    }

    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
}

/**
 * @brief 规范化Telegram聊天ID（支持
 *
 * @username、纯数字、科学记数法）
 * @param[in] in 原始聊天ID
 * @param[out] out 规范化输出缓冲区
 * @param[in] out_size 输出缓冲区大小
 * @return true成功，false失败
 */
static bool tg_normalize_chat_id(const char *in, char *out, size_t out_size)
{
    char tmp[48];
    char *end = NULL;
    double dv;
    long long ll;

    if (!in || !out || out_size == 0) {
        return false;
    }

    safe_copy(tmp, sizeof(tmp), in);
    tg_trim_spaces(tmp);
    if (tmp[0] == '\0') {
        return false;
    }

    /* Telegram also allows channel usernames like @channel_name. */
    if (tmp[0] == '@') {
        safe_copy(out, out_size, tmp);
        return true;
    }

    if (tg_is_integer_string(tmp)) {
        safe_copy(out, out_size, tmp);
        return true;
    }

    /* Recover numeric IDs serialized in scientific notation, e.g. "9e+09". */
    dv = strtod(tmp, &end);
    if (end && *end == '\0') {
        if (dv >= 0.0) {
            ll = (long long)(dv + 0.5);
        } else {
            ll = (long long)(dv - 0.5);
        }
        snprintf(out, out_size, "%lld", ll);
        return true;
    }

    safe_copy(out, out_size, tmp);
    return true;
}

/**
 * @brief 解析字符串为64位有符号整数
 *
 * @param[in] s 待解析字符串
 * @param[out] out 解析结果
 * @return true成功，false失败
 */
static bool tg_parse_i64(const char *s, long long *out)
{
    char *end = NULL;
    long long v;

    if (!s || !out || s[0] == '\0') {
        return false;
    }

    errno = 0;
    v = strtoll(s, &end, 10);
    if (errno != 0 || !end || *end != '\0') {
        return false;
    }

    *out = v;
    return true;
}

/**
 * @brief 调试用：调用getChat API获取聊天信息
 *
 * @param[in] chat_id 聊天ID
 * @param[in] numeric_chat_id 是否以数字方式传递chat_id
 */
static void tg_debug_get_chat_once(const char *chat_id, bool numeric_chat_id)
{
    char payload[96];
    char *resp = NULL;
    int status = 0;
    int n;

    if (!chat_id || chat_id[0] == '\0') {
        return;
    }

    if (numeric_chat_id) {
        n = snprintf(payload, sizeof(payload), "{\"chat_id\":%s}", chat_id);
    } else {
        n = snprintf(payload, sizeof(payload), "{\"chat_id\":\"%s\"}", chat_id);
    }
    if (n <= 0 || n >= (int)sizeof(payload)) {
        AXK_LOG_WARN("warn", "skip getChat debug: payload overflow");
        return;
    }

    if (tg_api_call("getChat", payload, &resp, &status) != 0) {
        AXK_LOG_WARN("warn", "getChat debug request failed for chat_id=%s", chat_id);
        return;
    }

    AXK_LOG_WARN("warn", "getChat debug(%s) chat_id=%s status=%d body=%.180s",
             numeric_chat_id ? "num" : "str",
             chat_id,
             status,
             resp ? resp : "");
    free(resp);
}

/**
 * @brief 调试用：分别以字符串和数字方式获取聊天信息
 *
 * @param[in] chat_id 聊天ID
 * @param[in] numeric_chat_id 是否同时尝试数字方式
 */
static void tg_debug_get_chat(const char *chat_id, bool numeric_chat_id)
{
    tg_debug_get_chat_once(chat_id, false);
    if (numeric_chat_id) {
        tg_debug_get_chat_once(chat_id, true);
    }
}

/**
 * @brief 追加响应数据到Telegram HTTP响应缓冲区
 *
 * @param[in] rb 响应缓冲区指针
 * @param[in] data 要追加的数据
 * @param[in] len 数据长度
 * @return 0成功，-1内存不足
 */
static int tg_resp_append(tg_http_resp_t *rb, const uint8_t *data, size_t len)
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

/**
 * @brief Telegram HTTP响应回调，逐片接收响应体并追加到缓冲区
 *
 * @param[in] rsp HTTP响应结构
 * @param[in] final_data 是否最后一片数据
 * @param[in] user_data 用户数据指针（tg_http_resp_t）
 */
static void tg_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    tg_http_resp_t *rb = (tg_http_resp_t *)user_data;
    (void)final_data;

    if (!rb || !rsp || rb->oom) {
        return;
    }

    if (rb->status_code == 0 && rsp->http_status_code > 0) {
        rb->status_code = rsp->http_status_code;
    }

    if (rsp->body_frag_start && rsp->body_frag_len > 0) {
        (void)tg_resp_append(rb, rsp->body_frag_start, rsp->body_frag_len);
    }
}

/**
 * @brief 调用Telegram Bot API（带重试）
 *
 * @param[in] method API方法名（如getUpdates、sendMessage）
 * @param[in] post_data POST请求JSON体（NULL则GET）
 * @param[out] out_body 响应JSON体（调用者需free）
 * @param[out] out_status HTTP状态码
 * @return 0成功，-1失败，-2缺少token
 */
static int tg_api_call(const char *method, const char *post_data, char **out_body, int *out_status)
{
    char url[384];
    const char *headers_get[] = {
        "Accept: application/json\r\n",
        "Connection: close\r\n",
        NULL
    };
    const char *headers_post[] = {
        "Accept: application/json\r\n",
        "Connection: close\r\n",
        NULL
    };
    struct https_client_request req = { 0 };
    int ret = -1;
    int attempt;

    if (!method || !out_body) {
        return -1;
    }

    *out_body = NULL;
    if (out_status) {
        *out_status = 0;
    }

    if (s_bot_token[0] == '\0') {
        return -2;
    }

    if (snprintf(url, sizeof(url), "https://api.telegram.org/bot%s/%s", s_bot_token, method) >= (int)sizeof(url)) {
        return -1;
    }

    req.method = post_data ? HTTP_POST : HTTP_GET;
    req.url = url;
    req.protocol = "HTTP/1.1";
    req.response = tg_http_response_cb;
    req.header_fields = post_data ? headers_post : headers_get;
    req.buffer_size = 2048;

    if (post_data) {
        req.content_type_value = "application/json";
        req.payload = post_data;
        req.payload_len = strlen(post_data);
    }

    for (attempt = 1; attempt <= TG_HTTP_REQ_RETRY; attempt++) {
        tg_http_resp_t rb = { 0 };

        rb.data = (char *)calloc(1, TG_HTTP_RESP_INIT_CAP);
        if (!rb.data) {
            return -1;
        }
        rb.cap = TG_HTTP_RESP_INIT_CAP;

        ret = https_client_request(&req, TG_HTTP_TIMEOUT_MS, &rb);
        if (ret > 0 && !rb.oom && rb.status_code > 0) {
            if (out_status) {
                *out_status = rb.status_code;
            }
            *out_body = rb.data;
            return 0;
        }

        free(rb.data);
        if (attempt < TG_HTTP_REQ_RETRY) {
            AXK_LOG_WARN("warn", "%s request invalid ret=%d status=%d body_len=%u, retry %d/%d",
                     method,
                     ret,
                     rb.status_code,
                     (unsigned int)rb.len,
                     attempt + 1,
                     TG_HTTP_REQ_RETRY);
            axk_mimiclaw_port_sleep_ms(TG_HTTP_REQ_RETRY_DELAY_MS);
        }
    }

    return -1;
}

/**
 * @brief 检查Telegram API响应中ok字段是否为true
 *
 * @param[in] body API响应JSON字符串
 * @return true表示ok，false表示失败
 */
static bool tg_response_is_ok(const char *body)
{
    bool ok = false;
    cJSON *root;
    cJSON *ok_item;

    if (!body) {
        return false;
    }

    root = cJSON_Parse(body);
    if (!root) {
        return (strstr(body, "\"ok\":true") != NULL);
    }

    ok_item = cJSON_GetObjectItem(root, "ok");
    ok = cJSON_IsTrue(ok_item);
    cJSON_Delete(root);
    return ok;
}

/**
 * @brief 从JSON中解析Telegram聊天ID
 *
 * @param[in] chat_id cJSON聊天ID节点
 * @param[out] out 输出缓冲区
 * @param[in] out_size 输出缓冲区大小
 * @return true成功，false失败
 */
static bool tg_parse_chat_id(const cJSON *chat_id, char *out, size_t out_size)
{
    char tmp[48];
    double dv;
    long long iv;

    if (!chat_id || !out || out_size == 0) {
        return false;
    }

    if (cJSON_IsString(chat_id) && chat_id->valuestring && chat_id->valuestring[0] != '\0') {
        return tg_normalize_chat_id(chat_id->valuestring, out, out_size);
    }

    if (cJSON_IsNumber(chat_id)) {
        /* Avoid %f formatting path: on some libc configs it can stringify
         * large numbers as scientific notation with coarse precision.
         * Convert to integer first, then print as decimal string.
         */
        dv = chat_id->valuedouble;
        if (dv >= 0.0) {
            iv = (long long)(dv + 0.5);
        } else {
            iv = (long long)(dv - 0.5);
        }
        snprintf(tmp, sizeof(tmp), "%lld", iv);
        return tg_normalize_chat_id(tmp, out, out_size);
    }

    return false;
}

/**
 * @brief 格式化聊天ID用于调试日志输出
 *
 * @param[in] item cJSON聊天ID节点
 * @param[out] buf 输出缓冲区
 * @param[in] buf_size 缓冲区大小
 */
static void tg_format_chat_id_debug(const cJSON *item, char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0) {
        return;
    }
    buf[0] = '\0';

    if (!item) {
        snprintf(buf, buf_size, "(null)");
        return;
    }

    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(buf, buf_size, "str:%s", item->valuestring);
        return;
    }

    if (cJSON_IsNumber(item)) {
        snprintf(buf, buf_size, "num:%.17g", item->valuedouble);
        return;
    }

    snprintf(buf, buf_size, "type:%d", item->type);
}

/**
 * @brief 将update偏移量持久化到NVS（满足间隔条件时）
 *
 * @param[in] force 强制保存（忽略间隔条件）
 */
static void tg_save_offset_if_needed(bool force)
{
    bool need_save = force;
    uint32_t now;

    if (s_update_offset <= 0) {
        return;
    }

    now = axk_mimiclaw_port_uptime_ms();
    if (!need_save) {
        if (s_last_saved_offset < 0) {
            need_save = true;
        } else if ((s_update_offset - s_last_saved_offset) >= TG_OFFSET_SAVE_STEP) {
            need_save = true;
        } else if ((uint32_t)(now - s_last_offset_save_ms) >= TG_OFFSET_SAVE_INTERVAL_MS) {
            need_save = true;
        }
    }

    if (!need_save) {
        return;
    }

    if (axk_kv_set_blob(MIMICLAW_KV_TG_OFFSET, &s_update_offset, sizeof(s_update_offset)) == 0) {
        s_last_saved_offset = s_update_offset;
        s_last_offset_save_ms = now;
    }
}

/**
 * @brief 解析Telegram getUpdates响应JSON，提取消息并推入消息总线
 *
 * @param[in] json_str getUpdates的响应JSON字符串
 */
static void tg_process_updates(const char *json_str)
{
    cJSON *root;
    cJSON *ok;
    cJSON *result;
    cJSON *update;
    bool offset_moved = false;
    bool stop_polling_batch = false;

    if (!json_str || json_str[0] == '\0') {
        return;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        AXK_LOG_WARN("warn", "parse getUpdates response failed");
        return;
    }

    ok = cJSON_GetObjectItem(root, "ok");
    result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsTrue(ok) || !cJSON_IsArray(result)) {
        cJSON_Delete(root);
        return;
    }

    cJSON_ArrayForEach(update, result)
    {
        cJSON *update_id = cJSON_GetObjectItem(update, "update_id");
        cJSON *message;
        cJSON *text;
        cJSON *chat;
        cJSON *chat_id;
        cJSON *from;
        cJSON *from_id;
        cJSON *chat_type_item;
        int64_t uid = -1;
        int64_t next_offset = -1;
        bool advance_offset = false;
        char chat_id_str[32];
        char from_id_str[32];
        char chat_id_dbg[64];
        char from_id_dbg[64];
        bool has_chat_id = false;
        bool has_from_id = false;
        const char *chat_type = NULL;
        const char *reply_id = NULL;
        mimi_msg_t msg = { 0 };

        if (cJSON_IsNumber(update_id)) {
            uid = (int64_t)update_id->valuedouble;
            if (uid < s_update_offset) {
                continue;
            }
            next_offset = uid + 1;
            advance_offset = true;
        }

        message = cJSON_GetObjectItem(update, "message");
        if (!message) {
            if (advance_offset && next_offset > s_update_offset) {
                s_update_offset = next_offset;
                offset_moved = true;
            }
            continue;
        }

        text = cJSON_GetObjectItem(message, "text");
        if (!cJSON_IsString(text) || !text->valuestring) {
            if (advance_offset && next_offset > s_update_offset) {
                s_update_offset = next_offset;
                offset_moved = true;
            }
            continue;
        }

        chat = cJSON_GetObjectItem(message, "chat");
        if (!chat) {
            if (advance_offset && next_offset > s_update_offset) {
                s_update_offset = next_offset;
                offset_moved = true;
            }
            continue;
        }
        from = cJSON_GetObjectItem(message, "from");

        chat_id = cJSON_GetObjectItem(chat, "id");
        tg_format_chat_id_debug(chat_id, chat_id_dbg, sizeof(chat_id_dbg));
        if (tg_parse_chat_id(chat_id, chat_id_str, sizeof(chat_id_str))) {
            has_chat_id = true;
        }

        from_id = from ? cJSON_GetObjectItem(from, "id") : NULL;
        tg_format_chat_id_debug(from_id, from_id_dbg, sizeof(from_id_dbg));
        if (tg_parse_chat_id(from_id, from_id_str, sizeof(from_id_str))) {
            has_from_id = true;
        }

        chat_type_item = cJSON_GetObjectItem(chat, "type");
        if (cJSON_IsString(chat_type_item) && chat_type_item->valuestring) {
            chat_type = chat_type_item->valuestring;
        }

        if (has_chat_id) {
            reply_id = chat_id_str;
        } else if (has_from_id) {
            reply_id = from_id_str;
        } else {
            if (advance_offset && next_offset > s_update_offset) {
                s_update_offset = next_offset;
                offset_moved = true;
            }
            continue;
        }

        strncpy(msg.channel, MIMI_CHAN_TELEGRAM, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, reply_id, sizeof(msg.chat_id) - 1);
    msg.content = text->valuestring;
    msg.priority = MIMI_PRIO_NORMAL;  /**< Telegram usermsg */
    if (axk_message_bus_push_inbound(&msg) != 0) {
            AXK_LOG_WARN("warn", "Inbound queue full, drop telegram message");
            stop_polling_batch = true;
            break;
        }

        AXK_LOG_INFO("info",
                 "queued telegram message reply_id=%s chat.id=%s (%s) from.id=%s (%s) type=%s len=%u",
                 msg.chat_id,
                 has_chat_id ? chat_id_str : "(none)",
                 chat_id_dbg,
                 has_from_id ? from_id_str : "(none)",
                 from_id_dbg,
                 chat_type ? chat_type : "(none)",
                 (unsigned int)strlen(msg.content));

        if (advance_offset && next_offset > s_update_offset) {
            s_update_offset = next_offset;
            offset_moved = true;
        }
    }

    if (stop_polling_batch) {
        AXK_LOG_WARN("warn", "stop processing current updates batch, keep offset=%lld for retry",
                 (long long)s_update_offset);
    }

    if (offset_moved) {
        tg_save_offset_if_needed(false);
    }
    cJSON_Delete(root);
}

/**
 * @brief Telegram长轮询任务：循环调用getUpdates获取新消息
 *
 * @param[in] arg 任务参数（未使用）
 */
static void telegram_poll_task(void *arg)
{
    (void)arg;
    AXK_LOG_INFO("info", "Telegram polling task started");

    while (1) {
        char payload[128];
        char fallback_payload[128];
        char *body = NULL;
        int status = 0;

        if (s_bot_token[0] == '\0') {
            static bool s_no_token_warned = false;
            if (!s_no_token_warned) {
                AXK_LOG_WARN("warn", "No Telegram bot token configured");
                s_no_token_warned = true;
            }
            axk_mimiclaw_port_sleep_ms(TG_NO_TOKEN_BACKOFF_MS);
            continue;
        }

        snprintf(payload, sizeof(payload),
                 "{\"offset\":%lld,\"timeout\":%d}",
                 (long long)s_update_offset, MIMI_TG_POLL_TIMEOUT_S);

        if (tg_api_call("getUpdates", payload, &body, &status) != 0) {
            axk_mimiclaw_port_sleep_ms(TG_POLL_ERROR_BACKOFF_MS);
            continue;
        }

        if (status == 200 && body && body[0] != '\0') {
            tg_process_updates(body);
            free(body);
            continue;
        }

        free(body);
        body = NULL;
        status = 0;

        snprintf(fallback_payload, sizeof(fallback_payload),
                 "{\"offset\":%lld,\"timeout\":%d}",
                 (long long)s_update_offset, TG_POLL_FALLBACK_TIMEOUT_S);

        if (tg_api_call("getUpdates", fallback_payload, &body, &status) == 0 &&
            status == 200 && body && body[0] != '\0') {
            AXK_LOG_WARN("warn", "getUpdates long-poll fallback -> short-poll");
            tg_process_updates(body);
            free(body);
            continue;
        }

        AXK_LOG_WARN("warn", "getUpdates failed long/short poll, short_status=%d body=%.120s",
                 status, body ? body : "");
        free(body);
        axk_mimiclaw_port_sleep_ms(TG_POLL_ERROR_BACKOFF_MS);
    }
}

/**
 * @brief 初始化Telegram Bot模块，从NVS加载token和offset
 *
 * @return 0成功
 */
int axk_telegram_bot_init(void)
{
    char token_buf[TG_TOKEN_MAX_LEN] = { 0 };
    size_t out_len = 0;
    int64_t offset = 0;

    if (axk_kv_get_blob(MIMICLAW_KV_TG_TOKEN, token_buf, sizeof(token_buf), &out_len) == 0 &&
        out_len > 1 && token_buf[0] != '\0') {
        safe_copy(s_bot_token, sizeof(s_bot_token), token_buf);
    }

    if (axk_kv_get_blob(MIMICLAW_KV_TG_OFFSET, &offset, sizeof(offset), &out_len) == 0 &&
        out_len == sizeof(offset) && offset > 0) {
        s_update_offset = offset;
        s_last_saved_offset = offset;
        s_last_offset_save_ms = axk_mimiclaw_port_uptime_ms();
        AXK_LOG_INFO("info", "loaded telegram offset=%lld", (long long)offset);
    }

    if (s_bot_token[0] != '\0') {
        AXK_LOG_INFO("info", "Telegram bot token loaded (len=%u)", (unsigned int)strlen(s_bot_token));
    } else {
        AXK_LOG_WARN("warn", "No Telegram bot token, use mimiclaw_set_tg_token <token>");
    }

    return 0;
}

/**
 * @brief 启动Telegram长轮询任务
 *
 * @return 0成功，-1任务创建失败
 */
int axk_telegram_bot_start(void)
{
    if (s_poll_started) {
        return 0;
    }

    if (xTaskCreate(telegram_poll_task, "tg_poll", MIMI_TG_POLL_STACK, NULL,
                    MIMI_TG_POLL_PRIO, &s_poll_task) != pdPASS) {
        s_poll_task = NULL;
        return -1;
    }

    s_poll_started = true;
    return 0;
}

/**
 * @brief 发送文本消息到Telegram聊天（自动分段长消息）
 *
 * @param[in] chat_id 聊天ID
 * @param[in] text 消息文本内容
 * @return 0成功，-1失败，-2缺少token
 */
int axk_telegram_send_message(const char *chat_id, const char *text)
{
    char normalized_chat_id[32];
    long long numeric_chat_id = 0;
    bool chat_id_is_numeric = false;
    size_t text_len;
    size_t offset = 0;
    bool all_ok = true;

    if (!chat_id || chat_id[0] == '\0' || !text) {
        return -1;
    }
    if (s_bot_token[0] == '\0') {
        return -2;
    }
    if (!tg_normalize_chat_id(chat_id, normalized_chat_id, sizeof(normalized_chat_id)) ||
        normalized_chat_id[0] == '\0') {
        return -1;
    }
    chat_id_is_numeric = tg_parse_i64(normalized_chat_id, &numeric_chat_id);

    text_len = strlen(text);
    if (text_len == 0) {
        return 0;
    }

    while (offset < text_len) {
        size_t chunk = text_len - offset;
        char *segment;
        cJSON *body_json;
        cJSON *fallback_body_json;
        char *payload;
        char *fallback_payload;
        char *resp = NULL;
        char *fallback_resp = NULL;
        int status = 0;
        int fallback_status = 0;
        bool sent_ok = false;
        bool send_numeric_first = false;

        if (chunk > MIMI_TG_MAX_MSG_LEN) {
            chunk = MIMI_TG_MAX_MSG_LEN;
        }

        segment = (char *)malloc(chunk + 1);
        if (!segment) {
            return -1;
        }
        memcpy(segment, text + offset, chunk);
        segment[chunk] = '\0';

        body_json = cJSON_CreateObject();
        if (!body_json) {
            free(segment);
            return -1;
        }
        send_numeric_first = chat_id_is_numeric && normalized_chat_id[0] != '@';
        if (send_numeric_first) {
            cJSON_AddRawToObject(body_json, "chat_id", normalized_chat_id);
        } else {
            cJSON_AddStringToObject(body_json, "chat_id", normalized_chat_id);
        }
        cJSON_AddStringToObject(body_json, "text", segment);
        payload = cJSON_PrintUnformatted(body_json);
        cJSON_Delete(body_json);

        if (!payload) {
            free(segment);
            all_ok = false;
            offset += chunk;
            continue;
        }

        if (tg_api_call("sendMessage", payload, &resp, &status) != 0 ||
            status != 200 || !tg_response_is_ok(resp)) {
            AXK_LOG_ERROR("err", "sendMessage failed chat_id=%s status=%d body=%.120s",
                     normalized_chat_id, status, resp ? resp : "");
            if (chat_id_is_numeric) {
                fallback_body_json = cJSON_CreateObject();
                fallback_payload = NULL;
                fallback_resp = NULL;
                fallback_status = 0;

                if (fallback_body_json) {
                    if (send_numeric_first) {
                        cJSON_AddStringToObject(fallback_body_json, "chat_id", normalized_chat_id);
                    } else {
                        cJSON_AddRawToObject(fallback_body_json, "chat_id", normalized_chat_id);
                    }
                    cJSON_AddStringToObject(fallback_body_json, "text", segment);
                    fallback_payload = cJSON_PrintUnformatted(fallback_body_json);
                    cJSON_Delete(fallback_body_json);
                }

                if (fallback_payload &&
                    tg_api_call("sendMessage", fallback_payload, &fallback_resp, &fallback_status) == 0 &&
                    fallback_status == 200 && tg_response_is_ok(fallback_resp)) {
                    AXK_LOG_WARN("warn", "sendMessage fallback(%s) success chat_id=%s",
                             send_numeric_first ? "str" : "num", normalized_chat_id);
                    sent_ok = true;
                } else {
                    AXK_LOG_WARN("warn", "sendMessage fallback(%s) failed chat_id=%s status=%d body=%.120s",
                             send_numeric_first ? "str" : "num",
                             normalized_chat_id,
                             fallback_status,
                             fallback_resp ? fallback_resp : "");
                }

                free(fallback_payload);
                free(fallback_resp);
            }

            if (!sent_ok && status == 400 && resp && strstr(resp, "chat not found")) {
                tg_debug_get_chat(normalized_chat_id, chat_id_is_numeric);
            }
            if (!sent_ok) {
                all_ok = false;
            }
        } else {
            sent_ok = true;
        }

        free(resp);
        free(payload);
        free(segment);
        offset += chunk;
    }

    return all_ok ? 0 : -1;
}

/**
 * @brief 设置并持久化Telegram Bot Token到NVS
 *
 * @param[in] token Bot Token字符串
 * @return 0成功，-1失败（参数无效或写入NVS失败）
 */
int axk_telegram_set_token(const char *token)
{
    size_t len;

    if (!token || token[0] == '\0') {
        return -1;
    }

    len = strlen(token);
    if (len >= sizeof(s_bot_token)) {
        return -1;
    }

    if (axk_kv_set_blob(MIMICLAW_KV_TG_TOKEN, token, len + 1) != 0) {
        return -1;
    }

    safe_copy(s_bot_token, sizeof(s_bot_token), token);
    AXK_LOG_INFO("info", "Telegram bot token saved");
    return 0;
}

/**
 * @brief 测试用：调用getMe API验证Bot Token是否有效
 *
 * @return 0成功，-1请求失败，-2缺少token
 */
int telegram_bot_test_get_me(void)
{
    char *body = NULL;
    int status = 0;

    if (s_bot_token[0] == '\0') {
        return -2;
    }

    if (tg_api_call("getMe", NULL, &body, &status) != 0) {
        AXK_LOG_ERROR("err", "getMe request failed");
        return -1;
    }

    if (status != 200 || !tg_response_is_ok(body)) {
        AXK_LOG_ERROR("err", "getMe failed status=%d body=%.200s", status, body ? body : "");
        free(body);
        return -1;
    }

    AXK_LOG_INFO("info", "getMe ok: %.200s", body ? body : "");
    free(body);
    return 0;
}
