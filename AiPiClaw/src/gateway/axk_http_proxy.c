/**
 * @file axk_http_proxy.c
 * @brief HTTP 代理module - 通用 HTTP/HTTPS 客户端封装
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 基于 SDK https_client 通用 HTTP request封装
 */

#include "axk_http_proxy.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "https_client.h"
#include "FreeRTOS.h"
#include "task.h"

#define HTTP_PROXY_TIMEOUT_MS   15000
#define HTTP_PROXY_RESP_CAP     (8 * 1024)

typedef struct {
    char *data;
    size_t len;       /**< 已接收数据长度 */
    size_t cap;       /**< 缓冲区总容量 */
    int status_code; /**< HTTP响应状态码 */
    bool oom;         /**< 内存不足标志 */
} http_proxy_resp_t;

/**
 * @brief 追加响应数据到HTTP代理响应缓冲区
 *
 * @param[in] rb 响应缓冲区指针
 * @param[in] data 要追加的数据
 * @param[in] len 数据长度
 * @return 0成功，-1内存不足
 */
static int http_proxy_resp_append(http_proxy_resp_t *rb, const uint8_t *data, size_t len)
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
 * @brief HTTP响应回调，逐片接收响应体并追加到缓冲区
 *
 * @param[in] rsp HTTP响应结构
 * @param[in] final_data 是否最后一片数据
 * @param[in] user_data 用户数据指针（http_proxy_resp_t）
 */
static void http_proxy_response_cb(struct http_response *rsp, enum http_final_call final_data, void *user_data)
{
    http_proxy_resp_t *rb = (http_proxy_resp_t *)user_data;
    (void)final_data;
    if (!rb || !rsp || rb->oom) {
        return;
    }
    if (rb->status_code == 0 && rsp->http_status_code > 0) {
        rb->status_code = rsp->http_status_code;
    }
    if (rsp->body_frag_start && rsp->body_frag_len > 0) {
        (void)http_proxy_resp_append(rb, rsp->body_frag_start, rsp->body_frag_len);
    }
}

/**
 * @brief 发起 HTTP/HTTPS request
 *
 * @param[in] url request URL（http://  or  https://）
 * @param[in] method "GET"  or  "POST"
 * @param[in] payload POST request体（JSON  or other ）
 * @param[in] content_type Content-Type 头
 * @param[out] out_body output 响\u5e94\u4f53（\u8c03\u7528\u8005\u9700 free）
 * @param[out] out_status output  HTTP 状\u6001\u7801
 * @return OKreturn  0，FAILreturn 非零
 */
int axk_http_request(const char *url, const char *method,
                      const char *payload, const char *content_type,
                      char **out_body, int *out_status)
{
    struct https_client_request req = {0};
    http_proxy_resp_t rb = {0};
    int ret;

    if (!url || !method || !out_body) {
        return -1;
    }

    *out_body = NULL;
    if (out_status) {
        *out_status = 0;
    }

    rb.data = (char *)calloc(1, HTTP_PROXY_RESP_CAP);
    if (!rb.data) {
        return -1;
    }
    rb.cap = HTTP_PROXY_RESP_CAP;

    if (strcasecmp(method, "POST") == 0) {
        req.method = HTTP_POST;
    } else {
        req.method = HTTP_GET;
    }
    req.url = url;
    req.protocol = "HTTP/1.1";
    req.response = http_proxy_response_cb;
    req.buffer_size = 2048;

    if (payload) {
        req.payload = payload;
        req.payload_len = strlen(payload);
        req.content_type_value = content_type ? content_type : "application/json";
    }

    ret = https_client_request(&req, HTTP_PROXY_TIMEOUT_MS, &rb);
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
 * @brief 初始化HTTP代理模块
 *
 * @return 0成功
 */
int axk_http_proxy_init(void)
{
    AXK_LOG_INFO("[axk_http_proxy] HTTP proxymoduleinitok\r\n");
    return 0;
}
