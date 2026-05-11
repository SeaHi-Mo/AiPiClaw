/**
 * @file axk_http_proxy.h
 * @brief http_proxy module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_HTTP_PROXY_H
#define __AXK_HTTP_PROXY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化HTTP代理模块
 * @return 0成功，负数错误码
 */
int axk_http_proxy_init(void);
/**
 * @brief 发送HTTP请求
 * @param[in] url 请求URL
 * @param[in] method HTTP方法（GET/POST）
 * @param[in] payload 请求体（GET时传NULL）
 * @param[in] content_type Content-Type头（如"application/json"）
 * @param[out] out_body 响应体（调用者需free）
 * @param[out] out_status HTTP状态码
 * @return 0成功，负数错误码
 */
int axk_http_request(const char *url, const char *method,
                      const char *payload, const char *content_type,
                      char **out_body, int *out_status);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_HTTP_PROXY_H */
