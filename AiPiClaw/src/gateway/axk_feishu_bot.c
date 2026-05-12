/**
 * @file axk_feishu_bot.c
 * @brief Feishu \u98de\u4e66 Bot \u6a21\u5757\u5b9e\u73b0 - \u5b89\u4fe1\u53ef\u79d1\u6280 BL618 \u79fb\u690d\u7248
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 \u5b89\u4fe1\u53ef\u79d1\u6280\u6709\u9650\u516c\u53f8
 * @note \u652f\u6301 tenant_access_token \u83b7\u53d6\u4e0e\u6587\u672c\u6d88\u606f\u53d1\u9001
 */

#include "axk_feishu_bot.h"
#include "axk_platform.h"
#include "axk_mimiclaw_port.h"  /**< axk_mimiclaw_port_uptime_ms() */
#include "axk_message_bus.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "https_client.h"
#include "cJSON.h"
#include "FreeRTOS.h"
#include "task.h"

static const char *TAG = "feishu";

#define FS_APP_ID_MAX_LEN       64
#define FS_APP_SECRET_MAX_LEN   64
#define FS_TOKEN_MAX_LEN        256
#define FS_HTTP_TIMEOUT_MS      10000
#define FS_HTTP_RESP_CAP        (4 * 1024)
#define FS_TOKEN_EXPIRY_MARGIN  60

typedef struct {
    char *data;
    size_t len;       /**< 已接收数据长度 */
    size_t cap;       /**< 缓冲区总容量 */
    int status_code; /**< HTTP响应状态码 */
    bool oom;         /**< 内存不足标志 */
} fs_http_resp_t;

static bool s_fs_initialized = false;
static char s_fs_app_id[FS_APP_ID_MAX_LEN] = "";
static char s_fs_app_secret[FS_APP_SECRET_MAX_LEN] = "";
static char s_fs_token[FS_TOKEN_MAX_LEN] = "";
static uint32_t s_fs_token_expire_at = 0;

/**
 * @brief 追加响应数据到飞书HTTP响应缓冲区
 *
 * @param[in] rb 响应缓冲区指针
 * @param[in] data 要追加的数据
 * @param[in] len 数据长度
 * @return 0成功，-1内存不足
 */
static int fs_resp_append(fs_http_resp_t *rb, const uint8_t *data, size_t len)
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
 * @brief 飞书HTTP响应回调，逐片接收响应体并追加到缓冲区
 *
 * @param[in] rsp HTTP响应结构
 * @param[in] final_data 是否最后一片数据
 * @param[in] user_data 用户数据指针（fs_http_resp_t）
 */
static void fs_http_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    fs_http_resp_t *rb = (fs_http_resp_t *)user_data;
    (void)final_data;
    if (!rb || !rsp || rb->oom) {
        return;
    }
    if (rb->status_code == 0 && rsp->http_status_code > 0) {
        rb->status_code = rsp->http_status_code;
    }
    if (rsp->body_frag_start && rsp->body_frag_len > 0) {
        (void)fs_resp_append(rb, rsp->body_frag_start, rsp->body_frag_len);
    }
}

/**
 * @brief \u53d1\u8d77 HTTPS \u8bf7\u6c42
 */
static int fs_https_post(const char *url, const char *payload,
                                const char *auth_token,
                                char **out_body, int *out_status)
{
    struct https_client_request req = {0};
    fs_http_resp_t rb = {0};
    int ret;
    char auth_hdr[512];
    const char *headers[4];
    int hdr_idx = 0;

    if (!url || !out_body) {
        return -1;
    }
    *out_body = NULL;
    if (out_status) {
        *out_status = 0;
    }

    rb.data = (char *)calloc(1, FS_HTTP_RESP_CAP);
    if (!rb.data) {
        return -1;
    }
    rb.cap = FS_HTTP_RESP_CAP;

    req.method = HTTP_POST;
    req.url = url;
    req.protocol = "HTTP/1.1";
    req.response = fs_http_response_cb;
    req.content_type_value = "application/json; charset=utf-8";
    req.payload = payload;
    req.payload_len = payload ? strlen(payload) : 0;
    req.buffer_size = 2048;

    headers[hdr_idx++] = "Accept: application/json\r\n";
    if (auth_token && auth_token[0]) {
        snprintf(auth_hdr, sizeof(auth_hdr), "Authorization: Bearer %s\r\n", auth_token);
        headers[hdr_idx++] = auth_hdr;
    }
    headers[hdr_idx++] = "Connection: close\r\n";
    headers[hdr_idx] = NULL;
    req.header_fields = headers;

    ret = https_client_request(&req, FS_HTTP_TIMEOUT_MS, &rb);
    if (ret > 0 && !rb.oom && rb.status_code > 0) {
        if (out_status) {
            *out_status = rb.status_code;
        }
        *out_body = rb.data;
        return 0;
    }
    free(rb.data);
    return -1;
}

/**
 * @brief \u83b7\u53d6 Feishu tenant_access_token
 *
 * @return \u6210\u529f\u8fd4\u56de 0
 */
static int fs_refresh_token(void)
{
    char url[] = "https://open.feishu.cn/open-apis/auth/v3/tenant_access_token/internal";
    char payload[256];
    char *body = NULL;
    int status = 0;
    cJSON *root, *code, *token, *expire;
    int err = -1;

    if (s_fs_app_id[0] == '\0' || s_fs_app_secret[0] == '\0') {
        AXK_LOG_ERROR("[%s] app_id \u6216 app_secret \u672a\u914d\u7f6e\r\n", TAG);
        return -2;
    }

    /* check  token is否仍有效 */
    uint32_t now = axk_mimiclaw_port_uptime_ms() / 1000;
    if (s_fs_token[0] && s_fs_token_expire_at > now + FS_TOKEN_EXPIRY_MARGIN) {
        return 0;
    }

    snprintf(payload, sizeof(payload),
             "{\"app_id\":\"%s\",\"app_secret\":\"%s\"}",
             s_fs_app_id, s_fs_app_secret);

    err = fs_https_post(url, payload, NULL, &body, &status);
    if (err != 0 || !body) {
        AXK_LOG_ERROR("[%s] \u83b7\u53d6 token \u8bf7\u6c42\u5931\u8d25\r\n", TAG);
        return -1;
    }

    root = cJSON_Parse(body);
    if (!root) {
        free(body);
        return -1;
    }

    code = cJSON_GetObjectItem(root, "code");
    token = cJSON_GetObjectItem(root, "tenant_access_token");
    expire = cJSON_GetObjectItem(root, "expire");

    if (!cJSON_IsNumber(code) || code->valueint != 0 ||
        !cJSON_IsString(token) || !token->valuestring) {
        AXK_LOG_ERROR("[%s] token \u54cd\u5e94\u683c\u5f0f\u5f02\u5e38: %s\r\n", TAG, body);
        cJSON_Delete(root);
        free(body);
        return -1;
    }

    strncpy(s_fs_token, token->valuestring, FS_TOKEN_MAX_LEN - 1);
    s_fs_token[FS_TOKEN_MAX_LEN - 1] = '\0';

    if (cJSON_IsNumber(expire)) {
        s_fs_token_expire_at = now + (uint32_t)expire->valueint;
    } else {
        s_fs_token_expire_at = now + 7200;
    }

    AXK_LOG_INFO("[%s] token \u5237\u65b0\u6210\u529f\uff0c\u8fc7\u671f\u65f6\u95f4: %lu\r\n", TAG, (unsigned long)s_fs_token_expire_at);
    cJSON_Delete(root);
    free(body);
    return 0;
}

/**
 * @brief 初始化飞书Bot模块
 *
 * @return 0成功
 */
int axk_feishu_bot_init(void)
{
    s_fs_initialized = true;
    /* 可从 KV 存储加载 app_id/app_secret */
    AXK_LOG_INFO("[%s] Feishu Bot 初始化完成\r\n", TAG);
    return 0;
}

/**
 * @brief 启动飞书Bot（消息发送与Webhook接收服务）
 *
 * @return 0成功，-1未初始化
 */
int axk_feishu_bot_start(void)
{
    if (!s_fs_initialized) {
        AXK_LOG_ERROR("[%s] \u8bf7\u5148\u8c03\u7528 init\r\n", TAG);
        return -1;
    }
    AXK_LOG_INFO("[%s] Feishu Bot \u542f\u52a8\uff08\u6d88\u606f\u53d1\u9001\u6a21\u5f0f\uff09\r\n", TAG);
    axk_feishu_bot_start_webhook();
    return 0;
}

/**
 * @brief 发送文本消息到飞书聊天
 *
 * @param[in] chat_id 聊天ID
 * @param[in] text 消息文本内容
 * @return 0成功，-1失败
 */
int axk_feishu_send_message(const char *chat_id, const char *text)
{
    char url[384];
    char payload[512];
    char content_json[256];
    char *body = NULL;
    int status = 0;
    int err;

    if (!s_fs_initialized || !chat_id || !text) {
        return -1;
    }

    err = fs_refresh_token();
    if (err != 0) {
        return -1;
    }

    /* 使用cJSON构建content JSON（安全转义，防止注入） */
    cJSON *content_root = cJSON_CreateObject();
    cJSON_AddStringToObject(content_root, "text", text);
    char *content_json_str = cJSON_PrintUnformatted(content_root);
    cJSON_Delete(content_root);
    if (!content_json_str) {
        return -1;
    }
    strncpy(content_json, content_json_str, sizeof(content_json) - 1);
    content_json[sizeof(content_json) - 1] = '\0';
    free(content_json_str);

    snprintf(url, sizeof(url),
             "https://open.feishu.cn/open-apis/im/v1/messages?receive_id_type=chat_id");

    snprintf(payload, sizeof(payload),
             "{\"receive_id\":\"%s\",\"msg_type\":\"text\",\"content\":%s}",
             chat_id, content_json);

    err = fs_https_post(url, payload, s_fs_token, &body, &status);
    if (err != 0) {
        AXK_LOG_ERROR("[%s] \u53d1\u9001\u6d88\u606f\u8bf7\u6c42\u5931\u8d25\r\n", TAG);
        return -1;
    }

    AXK_LOG_INFO("[%s] \u53d1\u9001\u6d88\u606f\u54cd\u5e94 status=%d body=%.120s\r\n", TAG, status, body ? body : "");
    free(body);
    return 0;
}

/**
 * @brief 设置飞书应用凭证（App ID 和 App Secret）
 *
 * @param[in] app_id 应用ID
 * @param[in] app_secret 应用密钥
 * @return 0成功，-1参数无效
 */
int axk_feishu_set_credentials(const char *app_id, const char *app_secret)
{
    if (!app_id || !app_secret) {
        return -1;
    }
    strncpy(s_fs_app_id, app_id, FS_APP_ID_MAX_LEN - 1);
    s_fs_app_id[FS_APP_ID_MAX_LEN - 1] = '\0';
    strncpy(s_fs_app_secret, app_secret, FS_APP_SECRET_MAX_LEN - 1);
    s_fs_app_secret[FS_APP_SECRET_MAX_LEN - 1] = '\0';
    s_fs_token[0] = '\0';
    s_fs_token_expire_at = 0;
    AXK_LOG_INFO("[%s] \u8bbe\u7f6e\u51ed\u8bc1: app_id=%s\r\n", TAG, s_fs_app_id);
    return 0;
}

/* ===================== Feishu Webhook recvservice器 ===================== */

#include "lwip/api.h"
#include "FreeRTOS.h"
#include "task.h"

#define FS_WEBHOOK_BACKLOG   4
#define FS_WEBHOOK_BUF_SIZE  4096

static TaskHandle_t s_fs_webhook_task = NULL;
static bool s_fs_webhook_running = false;
static struct netconn *s_fs_webhook_listener = NULL;

/**
 * @brief parse 飞书事件 JSON，提取msg and 推入message bus
 */
static void fs_process_event(const char *json_str)
{
    cJSON *root;
    cJSON *challenge;
    cJSON *header;
    cJSON *event;
    cJSON *message;
    cJSON *content;
    cJSON *text_obj;
    const char *text = NULL;
    const char *chat_id = NULL;

    if (!json_str || json_str[0] == '\0') {
        return;
    }

    root = cJSON_Parse(json_str);
    if (!root) {
        AXK_LOG_WARN("[%s] Webhook JSON parse 失\u8d25\r\n", TAG);
        return;
    }

    /* URL verify : 直\u63a5返\u56de challenge */
    challenge = cJSON_GetObjectItem(root, "challenge");
    if (cJSON_IsString(challenge) && challenge->valuestring) {
        AXK_LOG_INFO("[%s] 接\u6536\u5230\u98de\u4e66 URL \u9a8c\u8bc1 challenge=%s\r\n", TAG, challenge->valuestring);
        cJSON_Delete(root);
        return;
    }

    header = cJSON_GetObjectItem(root, "header");
    if (header) {
        cJSON *event_type = cJSON_GetObjectItem(header, "event_type");
        if (cJSON_IsString(event_type) && event_type->valuestring) {
            if (strcmp(event_type->valuestring, "im.message.receive_v1") != 0) {
                AXK_LOG_INFO("[%s] \u5ffd\u7565\u4e8b\u4ef6\u7c7b\u578b: %s\r\n", TAG, event_type->valuestring);
                cJSON_Delete(root);
                return;
            }
        }
    }

    event = cJSON_GetObjectItem(root, "event");
    if (event) {
        message = cJSON_GetObjectItem(event, "message");
        if (message) {
            cJSON *chat_id_json = cJSON_GetObjectItem(message, "chat_id");
            if (cJSON_IsString(chat_id_json) && chat_id_json->valuestring) {
                chat_id = chat_id_json->valuestring;
            }

            content = cJSON_GetObjectItem(message, "content");
            if (cJSON_IsString(content) && content->valuestring) {
                /* 飞\u4e66\u6d88\u606f content is JSON 字\u7b26\u4e32，\u5982 {"text":"hello"} */
                cJSON *content_json = cJSON_Parse(content->valuestring);
                if (content_json) {
                    text_obj = cJSON_GetObjectItem(content_json, "text");
                    if (cJSON_IsString(text_obj) && text_obj->valuestring) {
                        text = text_obj->valuestring;
                    }
                    cJSON_Delete(content_json);
                }
            }
        }
    }

    if (text && chat_id) {
        mimi_msg_t msg = {0};
        strncpy(msg.channel, MIMI_CHAN_FEISHU, sizeof(msg.channel) - 1);
        strncpy(msg.chat_id, chat_id, sizeof(msg.chat_id) - 1);
        msg.content = (char*)text;  /**< push_inbound internal strdup，const安全 */
        msg.priority = MIMI_PRIO_NORMAL;  /**< 飞书usermsg */
        if (axk_message_bus_push_inbound(&msg) != 0) {
            AXK_LOG_WARN("[%s] \u6d88\u606f\u603b\u7ebf\u6ee1，\u4e22\u5f03\u98de\u4e66\u6d88\u606f\r\n", TAG);
        } else {
            AXK_LOG_INFO("[%s] \u5df2\u63a5\u6536\u98de\u4e66\u6d88\u606f: %s\r\n", TAG, text);
        }
    }

    cJSON_Delete(root);
}

/**
 * @brief 处\u7406\u5355\u4e2a Webhook HTTP \u8fde\u63a5
 */
static void fs_webhook_handle_client(struct netconn *client)
{
    struct netbuf *buf = NULL;
    err_t err;
    char rx_buf[FS_WEBHOOK_BUF_SIZE];
    char *body = NULL;
    size_t body_len = 0;

    err = netconn_recv(client, &buf);
    if (err != ERR_OK || !buf) {
        netconn_close(client);
        netconn_delete(client);
        return;
    }

    char *data = NULL;
    u16_t len = 0;
    netbuf_data(buf, (void **)&data, &len);
    if (len > 0 && len < sizeof(rx_buf)) {
        memcpy(rx_buf, data, len);
        rx_buf[len] = '\0';

        /* 简\u5355\u89e3\u6790: \u67e5\u627e Content-Length \u5e76\u63d0\u53d6 body */
        char *cl = strstr(rx_buf, "Content-Length: ");
        if (cl) {
            long content_len = strtol(cl + 16, NULL, 10);
            char *blank = strstr(rx_buf, "\r\n\r\n");
            if (blank && content_len > 0) {
                size_t avail = FS_WEBHOOK_BUF_SIZE - (size_t)(blank + 4 - rx_buf);
                if ((size_t)content_len < avail) {
                    body = blank + 4;
                    body_len = content_len;
                    body[content_len] = '\0';
                } else {
                    AXK_LOG_WARN("[fs] webhook body too large: %ld > %zu bytes\r\n", content_len, avail);
                }
            }
        } else {
            /* 无 Content-Length，\u5c1d\u8bd5\u76f4\u63a5\u89e3\u6790 body */
            char *blank = strstr(rx_buf, "\r\n\r\n");
            if (blank) {
                body = blank + 4;
                body_len = strlen(body);
            }
        }
    }
    netbuf_delete(buf);

    if (body && body_len > 0) {
        fs_process_event(body);
    }

    /* 返\u56de HTTP 200 OK */
    const char *resp =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Connection: close\r\n"
        "Content-Length: 2\r\n"
        "\r\n"
        "{}";
    netconn_write(client, resp, strlen(resp), NETCONN_COPY);

    netconn_close(client);
    netconn_delete(client);
}

/**
 * @brief Feishu Webhook \u670d\u52a1\u5668\u4efb\u52a1
 */
static void fs_webhook_task(void *param)
{
    (void)param;
    struct netconn *listener = netconn_new(NETCONN_TCP);
    if (!listener) {
        AXK_LOG_ERROR("[%s] Webhook 创\u5efa netconn \u5931\u8d25\r\n", TAG);
        s_fs_webhook_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    netconn_bind(listener, IP_ADDR_ANY, MIMI_FEISHU_WEBHOOK_PORT);
    netconn_listen_with_backlog(listener, FS_WEBHOOK_BACKLOG);
    s_fs_webhook_listener = listener;
    AXK_LOG_INFO("[%s] Webhook \u670d\u52a1\u5668\u76d1\u542c\u7aef\u53e3 %d\r\n", TAG, MIMI_FEISHU_WEBHOOK_PORT);

    while (s_fs_webhook_running) {
        struct netconn *client = NULL;
        err_t err = netconn_accept(listener, &client);
        if (err != ERR_OK || !client) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        fs_webhook_handle_client(client);
    }

    netconn_close(listener);
    netconn_delete(listener);
    s_fs_webhook_listener = NULL;
    s_fs_webhook_task = NULL;
    vTaskDelete(NULL);
}

/**
 * @brief 启动飞书Webhook HTTP接收服务器（长连接轮询消息）
 *
 * @return 0成功，-1任务创建失败
 */
int axk_feishu_bot_start_webhook(void)
{
    if (s_fs_webhook_running) {
        return 0;
    }

    s_fs_webhook_running = true;
    if (xTaskCreate(fs_webhook_task, "fs_hook", 4096, NULL,
                    configMAX_PRIORITIES - 6, &s_fs_webhook_task) != pdPASS) {
        s_fs_webhook_running = false;
        s_fs_webhook_task = NULL;
        AXK_LOG_ERROR("[%s] Webhook \u4efb\u52a1\u521b\u5efa\u5931\u8d25\r\n", TAG);
        return -1;
    }

    AXK_LOG_INFO("[%s] Webhook \u670d\u52a1\u5df2\u542f\u52a8\r\n", TAG);
    return 0;
}
