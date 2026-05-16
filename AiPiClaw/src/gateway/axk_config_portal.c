/**
 * @file axk_config_portal.c
 * @brief Config Portal module - SoftAP HTTP配置门户
 * @version 1.0
 * @date 2026-05-15
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note 基于 lwIP netconn 实现 HTTP 服务器，提供配置页面和 REST API。
 *       上电无配置时启动 SoftAP + HTTP Server(:80)，用户配完自动转聊天。
 *       7 个 HTTP 路由：
 *         GET  /                    → config_ui.html (SPA配置页面)
 *         GET  /api/wifi/scan       → 扫描结果 JSON
 *         POST /api/wifi/connect    → 连接目标 WiFi
 *         GET  /api/wifi/status     → 当前 WiFi 状态
 *         POST /api/llm/config      → 设置 Provider/Model/Key
 *         GET  /api/llm/config      → 获取当前 LLM 配置
 *         POST /api/apply           → 保存配置 → 重启连接
 */

#include "axk_config_portal.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lwip/api.h"
#include "lwip/netif.h"
#include "lwip/ip4_addr.h"
#include "lwip/tcpip.h"
#include "lwip/ip_addr.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Watchdog for portal task — prevent lockup during scan/accept */
#include "bflb_wdg.h"

#include "cJSON.h"
#include "easyflash.h"
#include "wifi_mgmr_ext.h"
#include "axk_storage.h"

#include "axk_wifi_manager.h"
#include "axk_wifi_onboard.h"
#include "axk_llm_proxy.h"
#include "mimi_config.h"

#include "config_ui.h"
#include "web_ui.h"

/* ==================== Constants ==================== */

#define PORTAL_LISTEN_PORT      MIMI_ONBOARD_HTTP_PORT
#define PORTAL_LISTEN_BACKLOG   4
#define PORTAL_TASK_STACK       8192
#define PORTAL_TASK_PRIO        (configMAX_PRIORITIES - 4)
#define PORTAL_TIMEOUT_MS       10000
#define PORTAL_RX_BUF_SIZE      1024
#define PORTAL_MAX_HEADERS      4096

#define KV_WIFI_SSID            "mimi_wifi_ssid"
#define KV_WIFI_PASS            "mimi_wifi_pwd"
#define KV_LLM_API_KEY          "mimiclaw.llm.api_key"
#define KV_LLM_MODEL            "mimiclaw.llm.model"
#define KV_LLM_PROVIDER         "mimiclaw.llm.provider"

/* ==================== Internal State ==================== */

static bool s_portal_running = false;
static struct netconn *s_listener = NULL;
static TaskHandle_t s_portal_task = NULL;
static SemaphoreHandle_t s_portal_mutex = NULL;

/* Scan result cache (populated at boot and on refresh) */
static char *s_scan_json = NULL;
static bool s_scan_cached = false;   /**< true when cache has been filled at least once */
static SemaphoreHandle_t s_scan_mutex = NULL;

/* ==================== Public API ==================== */

static void portal_task(void *param);
static void handle_client(struct netconn *client);
static int handle_request(struct netconn *client,
                          const char *method, const char *uri,
                          const char *body, int body_len);
static int parse_request(struct netconn *client,
                         char *method, size_t method_sz,
                         char *uri, size_t uri_sz,
                         char *body, size_t body_sz);

/* Forward declarations for scan infrastructure */
static int trigger_scan_and_collect(void);
static void scan_item_callback(void *env, void *arg, wifi_mgmr_scan_item_t *item);

/* Route handlers */
static int handle_get_root(struct netconn *client);
static int handle_get_chat(struct netconn *client);
static int handle_wifi_scan(struct netconn *client);
static int handle_wifi_connect(struct netconn *client, const char *body);
static int handle_wifi_status(struct netconn *client);
static int handle_llm_config_get(struct netconn *client);
static int handle_llm_config_set(struct netconn *client, const char *body);
static int handle_apply(struct netconn *client, const char *body);

/* Response helpers */
static int send_response(struct netconn *client, int status,
                         const char *content_type,
                         const char *body);
static int send_json_response(struct netconn *client, int status,
                              const cJSON *json);
static int send_html_response(struct netconn *client, int status,
                              const char *html);

/* Forward declarations for scan infrastructure */
static int trigger_scan_and_collect(void);
static void scan_item_callback(void *env, void *arg, wifi_mgmr_scan_item_t *item);
static int portal_scan_and_cache(void);

/* ==================== Public API ==================== */

int axk_config_portal_init(void)
{
    if (s_portal_mutex == NULL) {
        s_portal_mutex = xSemaphoreCreateMutex();
    }
    if (s_scan_mutex == NULL) {
        s_scan_mutex = xSemaphoreCreateMutex();
    }
    AXK_LOG_INFO("[portal] Config Portal init ok\r\n");
    return 0;
}

int axk_config_portal_start(void)
{
    if (s_portal_running) {
        AXK_LOG_WARN("[portal] already running\r\n");
        return 0;
    }

    err_t err;
    struct netconn *listener = netconn_new(NETCONN_TCP);
    if (listener == NULL) {
        AXK_LOG_ERROR("[portal] netconn_new FAIL\r\n");
        return -1;
    }

    /* Find AP netif by IP 192.168.4.1 and bind to it */
    ip_addr_t ap_ip;
    ipaddr_aton("192.168.4.1", &ap_ip);
    LOCK_TCPIP_CORE();
    struct netif *iface = netif_find(NULL);
    struct netif *ap_netif = NULL;
    while (iface) {
        if (ip_addr_cmp(&iface->ip_addr, &ap_ip)) {
            ap_netif = iface;
            break;
        }
        iface = iface->next;
    }
    UNLOCK_TCPIP_CORE();

    if (ap_netif) {
        AXK_LOG_INFO("[portal] found AP netif %c%c, binding to %s\r\n",
                     ap_netif->name[0], ap_netif->name[1],
                     ipaddr_ntoa(&ap_ip));
        err = netconn_bind(listener, &ap_ip, PORTAL_LISTEN_PORT);
    } else {
        AXK_LOG_WARN("[portal] AP netif not found, binding IP_ANY\r\n");
        err = netconn_bind(listener, IP_ADDR_ANY, PORTAL_LISTEN_PORT);
    }
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[portal] netconn_bind port %d FAIL: %d\r\n",
                      PORTAL_LISTEN_PORT, err);
        netconn_delete(listener);
        return -1;
    }

    err = netconn_listen(listener);
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[portal] netconn_listen FAIL: %d\r\n", err);
        netconn_delete(listener);
        return -1;
    }

    s_listener = listener;
    s_portal_running = true;

    BaseType_t task_ret = xTaskCreate(
        portal_task,
        "cfg_portal",
        PORTAL_TASK_STACK,
        NULL,
        PORTAL_TASK_PRIO,
        &s_portal_task
    );
    if (task_ret != pdPASS) {
        AXK_LOG_ERROR("[portal] task create FAIL\r\n");
        netconn_delete(listener);
        s_listener = NULL;
        s_portal_running = false;
        return -1;
    }

    AXK_LOG_INFO("[portal] HTTP server started on port %d\r\n",
                 PORTAL_LISTEN_PORT);

    /* Step 1 (#68): Pre-scan WiFi and cache results for fast first page load */
    AXK_LOG_INFO("[portal] running pre-scan for cache...\r\n");
    portal_scan_and_cache();
    AXK_LOG_INFO("[portal] pre-scan done\r\n");

    return 0;
}

void axk_config_portal_stop(void)
{
    if (!s_portal_running) {
        return;
    }
    s_portal_running = false;

    /* Close listener to unblock netconn_accept */
    if (s_listener) {
        netconn_close(s_listener);
    }

    /* Free cached scan results */
    if (s_scan_mutex) {
        xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
        if (s_scan_json) {
            cJSON_free(s_scan_json);
            s_scan_json = NULL;
        }
        xSemaphoreGive(s_scan_mutex);
    }

    /* Wait for portal task to exit, then clean up */
    vTaskDelay(pdMS_TO_TICKS(100));
    if (s_listener) {
        netconn_delete(s_listener);
        s_listener = NULL;
    }

    AXK_LOG_INFO("[portal] Config Portal stopped\r\n");
}

bool axk_config_portal_is_running(void)
{
    return s_portal_running;
}

/* ==================== WiFi Scan Callback ==================== */

/**
 * @brief Callback for wifi_mgmr_scan_ap_all, builds JSON array of scan items.
 * Called synchronously inside wifi_mgmr_scan_ap_all loop.
 */
static void scan_item_callback(void *env, void *arg, wifi_mgmr_scan_item_t *item)
{
    (void)arg;
    cJSON *arr = (cJSON *)env;
    if (!arr || !item) {
        return;
    }

    /* Skip hidden SSIDs */
    if (item->ssid[0] == '\0') {
        return;
    }

    cJSON *ap = cJSON_CreateObject();
    cJSON_AddStringToObject(ap, "ssid", item->ssid);
    cJSON_AddNumberToObject(ap, "rssi", item->rssi);
    cJSON_AddNumberToObject(ap, "channel", item->channel);

    /* Security type string */
    const char *auth = "OPEN";
    switch (item->auth) {
        case 1: auth = "WEP"; break;
        case 2: auth = "WPA"; break;
        case 3: auth = "WPA2"; break;
        case 4: auth = "WPA3"; break;
        case 5: auth = "WPA2_WPA3"; break;
        default: auth = "UNKNOWN"; break;
    }
    cJSON_AddStringToObject(ap, "auth", auth);

    /* Format BSSID */
    char bssid_str[18];
    snprintf(bssid_str, sizeof(bssid_str),
             "%02X:%02X:%02X:%02X:%02X:%02X",
             item->bssid[0], item->bssid[1], item->bssid[2],
             item->bssid[3], item->bssid[4], item->bssid[5]);
    cJSON_AddStringToObject(ap, "bssid", bssid_str);

    cJSON_AddItemToArray(arr, ap);
}

/* ==================== Scan Callback Wrapper ==================== */

/**
 * @brief Scan trigger + callback. wifi_mgmr_sta_scan is async; we call it,
 * then wait a bit, then iterate results via wifi_mgmr_scan_ap_all.
 *
 * This is called from the HTTP handling context (portal task). We do a
 * short blocking wait with vTaskDelay because the scan completes via
 * async event (CODE_WIFI_ON_SCAN_DONE).
 *
 * @note Dual-mode (SoftAP + STA scan): On BL618 (WiFi6), the chipset
 * supports simultaneous SoftAP and STA scan. During scan, the AP channel
 * may briefly hop across other channels, potentially causing connected
 * clients to experience short latency spikes or packet loss. The scan
 * completes typically in 2-3 seconds. This is normal behavior.
 * After scan returns, the AP stabilizes back on its configured channel.
 */
static int trigger_scan_and_collect(void)
{
    /*
     * 5-step WiFi scan for fhost (WiFi6) mode:
     *   1. Stop SoftAP (free STA radio for scanning)
     *   2. Call wifi_mgmr_sta_scan
     *   3. Wait up to 5s for scan results
     *   4. Collect results
     *   5. Restart SoftAP
     *
     * On fhost precompiled library, wifi_mgmr_sta_scan requires the STA
     * VIF to exist and the radio to be available. With SoftAP active,
     * the fhost firmware refuses scan requests (returns -1). Stopping
     * AP releases the radio for the scan period, then AP restarts on
     * its original channel. Brief client disconnect (~5s) is expected.
     */
    wifi_mgmr_scan_params_t scan_params = { 0 };
    int ret;

    /* Read saved AP credentials from flash — fallback to defaults if none saved */
    char saved_ssid[64] = {0};
    char saved_key[64] = {0};
    wifi_mgmr_ap_params_t ap_cfg = { 0 };
    ap_cfg.ssid = "AiPiClaw";
    ap_cfg.key = "12345678";
    {
        size_t len = 0;
        if (ef_get_env_blob("mimi_wifi_ssid", saved_ssid, sizeof(saved_ssid), &len) == 0 && len > 0) {
            saved_ssid[sizeof(saved_ssid) - 1] = '\0';
            ap_cfg.ssid = saved_ssid;
            AXK_LOG_INFO("[portal] using saved SSID: %s\r\n", saved_ssid);
        }
        len = 0;
        if (ef_get_env_blob("mimi_wifi_pwd", saved_key, sizeof(saved_key), &len) == 0 && len > 0) {
            saved_key[sizeof(saved_key) - 1] = '\0';
            ap_cfg.key = saved_key;
        } else {
            ap_cfg.key = "12345678";
        }
    }
    ap_cfg.akm = "WPA2";
    ap_cfg.channel = 6;
    ap_cfg.use_dhcpd = true;
    ap_cfg.use_ipcfg = true;
    ap_cfg.ap_ipaddr = htonl(0xC0A80401);
    ap_cfg.ap_mask  = htonl(0xFFFFFF00);
    ap_cfg.start = 2;
    ap_cfg.limit = 4;

    /* Step 1: Stop SoftAP to free radio for STA scan */
    AXK_LOG_INFO("[portal] stopping SoftAP for scan...\r\n");
    int ap_stop_ret = wifi_mgmr_ap_stop();
    if (ap_stop_ret != 0) {
        AXK_LOG_ERROR("[portal] wifi_mgmr_ap_stop FAIL: %d, radio may be occupied, skip scan\r\n", ap_stop_ret);
        return -1;
    }
    vTaskDelay(pdMS_TO_TICKS(200));  /* wait for AP to fully teardown */

    /* Step 2: Trigger STA scan */
    scan_params.channels_cnt = 0;
    scan_params.passive = false;
    ret = wifi_mgmr_sta_scan(&scan_params);
    if (ret != 0) {
        AXK_LOG_ERROR("[portal] wifi_mgmr_sta_scan FAIL: %d\r\n", ret);
        goto restart_ap;
    }

    /* Step 3: Wait up to 5 seconds for scan results */
    {
        int wait_ms = 0;
        while (wait_ms < 5000) {
            vTaskDelay(pdMS_TO_TICKS(200));
            wait_ms += 200;
            /* Step 5 (#68): Feed watchdog during scan to prevent reset */
            struct bflb_device_s *wdg = bflb_device_get_by_name("watchdog0");
            if (wdg) {
                bflb_wdg_reset_countervalue(wdg);
            }
            uint32_t count = wifi_mgmr_sta_scanlist_nums_get();
            if (count > 0) {
                break;
            }
        }
    }

restart_ap:
    /* Step 5: Restart SoftAP (always, even if scan failed) */
    AXK_LOG_INFO("[portal] restarting SoftAP...\r\n");
    if (wifi_mgmr_ap_start(&ap_cfg) != 0) {
        AXK_LOG_ERROR("[portal] SoftAP restart FAIL\r\n");
    }
    vTaskDelay(pdMS_TO_TICKS(500));  /* wait for AP to be ready */

    return (ret == 0) ? 0 : -1;
}

/* ==================== Scan Cache ==================== */

/**
 * @brief Run a full scan and cache the JSON result.
 * Called at portal start and on explicit refresh.
 * @note AP is stopped during scan and restarted after.
 */
static int portal_scan_and_cache(void)
{
    cJSON *aps = cJSON_CreateArray();
    if (!aps) return -1;

    int ret = trigger_scan_and_collect();
    if (ret == 0) {
        wifi_mgmr_scan_ap_all((void *)aps, NULL, scan_item_callback);
    }
    /* Convert to string, cache it */
    char *json_str = cJSON_PrintUnformatted(aps);
    cJSON_Delete(aps);

    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    if (s_scan_json) cJSON_free(s_scan_json);
    s_scan_json = json_str;
    s_scan_cached = (json_str != NULL);
    xSemaphoreGive(s_scan_mutex);

    return (json_str != NULL) ? 0 : -1;
}

/* ==================== Request Parsing ==================== */
/**
 * @brief Read and parse HTTP request from client connection.
 *
 * Reads the request line and headers, then reads body if Content-Length
 * is present. Stores method, URI, and body in the output buffers.
 *
 * @return 0 on success, -1 on parse error / timeout
 */
static int parse_request(struct netconn *client,
                         char *method, size_t method_sz,
                         char *uri, size_t uri_sz,
                         char *body, size_t body_sz)
{
    struct netbuf *buf;
    char *data;
    u16_t len;
    int total = 0;
    char req_buf[PORTAL_MAX_HEADERS];
    bool headers_done = false;
    int content_length = 0;
    size_t uri_len = 0;

    body[0] = '\0';

    /* Read the request line + headers into a buffer */
    while (total < (int)sizeof(req_buf) - 1) {
        err_t err = netconn_recv(client, &buf);
        if (err != ERR_OK) {
            AXK_LOG_DEBUG("[portal] recv err %d\r\n", err);
            return -1;
        }

        do {
            netbuf_data(buf, (void **)&data, &len);
            if (len == 0) continue;

            if (total + (int)len > (int)sizeof(req_buf) - 1) {
                len = sizeof(req_buf) - 1 - total;
            }
            memcpy(req_buf + total, data, len);
            total += len;
            req_buf[total] = '\0';

            /* Check if we've found the end of headers */
            if (!headers_done) {
                char *hdr_end = strstr(req_buf, "\r\n\r\n");
                if (hdr_end) {
                    headers_done = true;
                    /* Parse Content-Length */
                    const char *cl = strstr(req_buf, "Content-Length:");
                    if (!cl) cl = strstr(req_buf, "content-length:");
                    if (!cl) cl = strstr(req_buf, "Content-length:");
                    if (cl) {
                        cl += 15; /* skip "Content-Length:" */
                        while (*cl == ' ') cl++;
                        content_length = atoi(cl);
                    }

                    /* Parse request line: METHOD /uri */
                    char *space = strchr(req_buf, ' ');
                    if (space) {
                        size_t mlen = space - req_buf;
                        if (mlen >= method_sz) mlen = method_sz - 1;
                        memcpy(method, req_buf, mlen);
                        method[mlen] = '\0';

                        char *uri_start = space + 1;
                        char *uri_end = strchr(uri_start, ' ');
                        if (uri_end) {
                            uri_len = uri_end - uri_start;
                            if (uri_len >= uri_sz) uri_len = uri_sz - 1;
                            memcpy(uri, uri_start, uri_len);
                            uri[uri_len] = '\0';
                        }
                    }
                }
            }

            if (headers_done && content_length > 0) {
                /* Check if we already have body data after headers */
                char *hdr_end_ptr = strstr(req_buf, "\r\n\r\n");
                if (hdr_end_ptr) {
                    char *body_start = hdr_end_ptr + 4;
                    int body_in_header = total - (int)(body_start - req_buf);
                    if (body_in_header > 0) {
                        int copy_len = body_in_header;
                        if (copy_len > (int)body_sz - 1)
                            copy_len = body_sz - 1;
                        memcpy(body, body_start, copy_len);
                        body[copy_len] = '\0';
                        if (copy_len >= content_length) {
                            netbuf_delete(buf);
                            return 0;
                        }
                    }
                }
            }
        } while (netbuf_next(buf) >= 0);
        netbuf_delete(buf);

        if (headers_done && content_length <= 0) {
            return 0;
        }
        if (headers_done && content_length > 0) {
            break;
        }
    }

    /* If we have Content-Length, read remaining body */
    if (content_length > 0) {
        int body_read = strlen(body);
        while (body_read < content_length) {
            err_t err = netconn_recv(client, &buf);
            if (err != ERR_OK) {
                return -1;
            }
            do {
                netbuf_data(buf, (void **)&data, &len);
                if (len > 0) {
                    int remain = content_length - body_read;
                    if ((int)len > remain) len = remain;
                    if (body_read + (int)len < (int)body_sz) {
                        memcpy(body + body_read, data, len);
                        body_read += len;
                        body[body_read] = '\0';
                    }
                }
            } while (netbuf_next(buf) >= 0);
            netbuf_delete(buf);
        }
    }

    return 0;
}

/* ==================== Response Helpers ==================== */

static int send_response(struct netconn *client, int status,
                         const char *content_type,
                         const char *body)
{
    char header[512];
    int header_len;

    const char *status_str = "OK";
    switch (status) {
        case 200: status_str = "OK"; break;
        case 201: status_str = "Created"; break;
        case 400: status_str = "Bad Request"; break;
        case 404: status_str = "Not Found"; break;
        case 405: status_str = "Method Not Allowed"; break;
        case 500: status_str = "Internal Server Error"; break;
        default: break;
    }

    int body_len = body ? (int)strlen(body) : 0;
    header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n",
        status, status_str,
        content_type ? content_type : "text/plain",
        body_len);

    err_t err = netconn_write(client, header, header_len, NETCONN_COPY);
    if (err != ERR_OK) {
        return -1;
    }

    if (body && body_len > 0) {
        err = netconn_write(client, body, body_len, NETCONN_COPY);
        if (err != ERR_OK) {
            return -1;
        }
    }

    return 0;
}

static int send_json_response(struct netconn *client, int status,
                              const cJSON *json)
{
    char *str = cJSON_PrintUnformatted(json);
    if (!str) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"json alloc fail\"}");
    }
    int ret = send_response(client, status,
                           "application/json; charset=utf-8", str);
    cJSON_free(str);
    return ret;
}

static int send_html_response(struct netconn *client, int status,
                              const char *html)
{
    return send_response(client, status,
                        "text/html; charset=utf-8", html);
}

/* ==================== Route Handlers ==================== */

/**
 * @brief GET / - Serve the SPA configuration page
 */
static int handle_get_root(struct netconn *client)
{
    return send_html_response(client, 200, CONFIG_UI_HTML);
}

/**
 * @brief GET /chat - Serve the Web Chat SPA page
 */
static int handle_get_chat(struct netconn *client)
{
    return send_html_response(client, 200, WEB_UI_HTML);
}

/**
 * @brief GET /api/wifi/scan - Return cached scan results.
 *
 * On first call after portal start, returns pre-cached results.
 * Supports ?refresh=1 to trigger a new real scan and update cache.
 * POST /api/wifi/scan acts as a refresh trigger (legacy compat).
 */
static int handle_wifi_scan(struct netconn *client)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    /* Always refresh the cache — the frontend polls after refresh trigger */
    AXK_LOG_INFO("[portal] refreshing scan cache...\r\n");
    int scan_ret = portal_scan_and_cache();
    AXK_LOG_INFO("[portal] scan cache refresh ret=%d\r\n", scan_ret);

    /* Return cached results wrapped in an object */
    xSemaphoreTake(s_scan_mutex, portMAX_DELAY);
    if (s_scan_json) {
        cJSON *cached = cJSON_Parse(s_scan_json);
        if (cached) {
            cJSON_Delete(resp);
            resp = cJSON_CreateObject();
            cJSON_AddItemToObject(resp, "aps", cached);
            cJSON_AddStringToObject(resp, "status", "ok");
            int r = send_json_response(client, 200, resp);
            cJSON_Delete(resp);
            xSemaphoreGive(s_scan_mutex);
            return r;
        }
    }

    cJSON_AddStringToObject(resp, "error", "scan_failed");
    int r = send_json_response(client, 500, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief POST /api/wifi/connect - Initiate WiFi connection
 *
 * JSON body: {"ssid":"...", "password":"..."}
 */
static int handle_wifi_connect(struct netconn *client, const char *body)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        cJSON_AddStringToObject(resp, "error", "invalid_json");
        int r = send_json_response(client, 400, resp);
        cJSON_Delete(resp);
        return r;
    }

    cJSON *ssid_j = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass_j = cJSON_GetObjectItem(json, "password");

    if (!ssid_j || !cJSON_IsString(ssid_j)) {
        cJSON_Delete(json);
        cJSON_AddStringToObject(resp, "error", "ssid_required");
        int r = send_json_response(client, 400, resp);
        cJSON_Delete(resp);
        return r;
    }

    const char *ssid = ssid_j->valuestring;
    const char *password = (pass_j && cJSON_IsString(pass_j))
                           ? pass_j->valuestring : "";

    /* Save credentials */
    int ret = axk_wifi_save_credentials(ssid, password);
    if (ret != 0) {
        cJSON_Delete(json);
        cJSON_AddStringToObject(resp, "error", "save_failed");
        int r = send_json_response(client, 500, resp);
        cJSON_Delete(resp);
        return r;
    }

    /* Initiate connection (async) */
    ret = axk_wifi_connect(ssid, password[0] ? password : NULL);
    if (ret != 0) {
        cJSON_AddStringToObject(resp, "error", "connect_failed");
    } else {
        cJSON_AddStringToObject(resp, "status", "connecting");
        cJSON_AddStringToObject(resp, "ssid", ssid);
    }

    cJSON_Delete(json);
    int r = send_json_response(client, ret == 0 ? 200 : 500, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief GET /api/wifi/status - Return current WiFi connection status
 */
static int handle_wifi_status(struct netconn *client)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    axk_wifi_state_t state = axk_wifi_get_state();
    const char *state_str = "disconnected";
    switch (state) {
        case AXK_WIFI_STATE_DISCONNECTED: state_str = "disconnected"; break;
        case AXK_WIFI_STATE_CONNECTING:   state_str = "connecting"; break;
        case AXK_WIFI_STATE_CONNECTED:    state_str = "connected"; break;
        case AXK_WIFI_STATE_GOT_IP:       state_str = "got_ip"; break;
    }
    cJSON_AddStringToObject(resp, "status", state_str);

    if (state >= AXK_WIFI_STATE_CONNECTED) {
        char ssid[64] = {0};
        char ip[32] = {0};
        int rssi = 0;
        if (axk_wifi_get_ssid(ssid, sizeof(ssid)) == 0) {
            cJSON_AddStringToObject(resp, "ssid", ssid);
        }
        if (axk_wifi_get_ip(ip, sizeof(ip)) == 0) {
            cJSON_AddStringToObject(resp, "ip", ip);
        }
        if (axk_wifi_get_rssi(&rssi) == 0) {
            cJSON_AddNumberToObject(resp, "rssi", rssi);
        }
    }

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief POST /api/llm/config - Set LLM configuration
 *
 * JSON body: {"provider":"...", "model":"...", "api_key":"..."}
 * All fields optional - only provided fields are updated.
 */
static int handle_llm_config_set(struct netconn *client, const char *body)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    cJSON *json = cJSON_Parse(body);
    if (!json) {
        cJSON_AddStringToObject(resp, "error", "invalid_json");
        int r = send_json_response(client, 400, resp);
        cJSON_Delete(resp);
        return r;
    }

    int errors = 0;

    cJSON *provider_j = cJSON_GetObjectItem(json, "provider");
    if (provider_j && cJSON_IsString(provider_j)) {
        if (axk_llm_set_provider(provider_j->valuestring) != 0) {
            errors++;
        }
    }

    cJSON *model_j = cJSON_GetObjectItem(json, "model");
    if (model_j && cJSON_IsString(model_j)) {
        if (axk_llm_set_model(model_j->valuestring) != 0) {
            errors++;
        }
    }

    cJSON *api_key_j = cJSON_GetObjectItem(json, "api_key");
    if (api_key_j && cJSON_IsString(api_key_j)) {
        if (axk_llm_set_api_key(api_key_j->valuestring) != 0) {
            errors++;
            AXK_LOG_ERROR("[portal] LLM set_api_key FAIL (len=%u)\r\n",
                          (unsigned int)strlen(api_key_j->valuestring));
        }
    }

    cJSON_Delete(json);

    if (errors > 0) {
        cJSON_AddStringToObject(resp, "status", "partial");
        cJSON_AddNumberToObject(resp, "errors", errors);
    } else {
        cJSON_AddStringToObject(resp, "status", "ok");
    }

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief GET /api/llm/config - Return current LLM config (masks API key)
 */
static int handle_llm_config_get(struct netconn *client)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    /* Read from storage directly */
    char provider[32] = {0};
    char model[128] = {0};
    char api_key[320] = {0};
    size_t len = 0;

    if (axk_kv_get_blob(KV_LLM_PROVIDER, provider, sizeof(provider), &len) == 0 && len > 0) {
        cJSON_AddStringToObject(resp, "provider", provider);
    }
    if (axk_kv_get_blob(KV_LLM_MODEL, model, sizeof(model), &len) == 0 && len > 0) {
        cJSON_AddStringToObject(resp, "model", model);
    }
    if (axk_kv_get_blob(KV_LLM_API_KEY, api_key, sizeof(api_key), &len) == 0 && len > 0) {
        /* Show only first 8 chars + "...", mask rest */
        char masked[32];
        size_t key_len = strnlen(api_key, sizeof(api_key));
        if (key_len > 8) {
            snprintf(masked, sizeof(masked), "%.8s...", api_key);
        } else {
            snprintf(masked, sizeof(masked), "%s", api_key);
        }
        cJSON_AddStringToObject(resp, "api_key_masked", masked);
        cJSON_AddBoolToObject(resp, "has_api_key", true);
    } else {
        cJSON_AddBoolToObject(resp, "has_api_key", false);
    }

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief POST /api/apply - Save all config and transition
 *
 * Saves WiFi + LLM config to flash, stops SoftAP and config portal,
 * then connects to target WiFi. WS server (already running from boot)
 * continues serving after STA connection is established.
 */
static int handle_apply(struct netconn *client, const char *body)
{
    (void)body;

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    /* Save everything to flash */
    ef_save_env();

    /* Keep HTTP server running — still serves /chat page.
     * SoftAP also stays up (AP+STA coexists).
     * User can still browse /chat after config applied. */

    /* WS server already running from modules_init; ensure it's listening */
    /* Attempt auto connect (reads saved WiFi credentials) */
    int ret = axk_wifi_auto_connect();

    cJSON_AddStringToObject(resp, "status", "applied");
    cJSON_AddStringToObject(resp, "wifi_connecting",
                            (ret == 0) ? "yes" : "no_credentials");
    cJSON_AddStringToObject(resp, "ws_port", "18789");
    cJSON_AddStringToObject(resp, "chat_url", "/chat");
    cJSON_AddStringToObject(resp, "apply_msg",
                            "Configuration saved. Browse /chat to start chatting.");

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/* ==================== Request Dispatcher ==================== */

static int handle_request(struct netconn *client,
                          const char *method, const char *uri,
                          const char *body, int body_len)
{
    (void)body_len;

    /* Extract query string (strip from URI for routing) */
    char uri_path[256];
    size_t i;
    for (i = 0; i < sizeof(uri_path) - 1 && uri[i] != '\0' && uri[i] != '?'; i++) {
        uri_path[i] = uri[i];
    }
    uri_path[i] = '\0';

    /* ------ Route matching ------ */

    /* GET / */
    if (strcmp(uri_path, "/") == 0) {
        if (strcmp(method, "GET") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_get_root(client);
    }

    /* GET /chat */
    if (strcmp(uri_path, "/chat") == 0) {
        if (strcmp(method, "GET") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_get_chat(client);
    }

    /* GET /api/wifi/scan */
    if (strcmp(uri_path, "/api/wifi/scan") == 0) {
        if (strcmp(method, "GET") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_wifi_scan(client);
    }

    /* POST /api/wifi/connect */
    if (strcmp(uri_path, "/api/wifi/connect") == 0) {
        if (strcmp(method, "POST") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_wifi_connect(client, body);
    }

    /* GET /api/wifi/status */
    if (strcmp(uri_path, "/api/wifi/status") == 0) {
        if (strcmp(method, "GET") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_wifi_status(client);
    }

    /* POST /api/llm/config */
    if (strcmp(uri_path, "/api/llm/config") == 0) {
        if (strcmp(method, "POST") == 0) {
            return handle_llm_config_set(client, body);
        } else if (strcmp(method, "GET") == 0) {
            return handle_llm_config_get(client);
        }
        return send_response(client, 405, "text/plain", "Method Not Allowed");
    }

    /* POST /api/apply */
    if (strcmp(uri_path, "/api/apply") == 0) {
        if (strcmp(method, "POST") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_apply(client, body);
    }

    /* 404 for unmatched routes */
    return send_response(client, 404, "text/plain", "Not Found");
}

/* ==================== Client Handler ==================== */

static void handle_client(struct netconn *client)
{
    char method[16] = {0};
    char uri[512] = {0};
    char body[PORTAL_MAX_HEADERS] = {0};

    int ret = parse_request(client, method, sizeof(method),
                            uri, sizeof(uri), body, sizeof(body));
    if (ret != 0) {
        netconn_close(client);
        netconn_delete(client);
        return;
    }

    if (method[0] == '\0') {
        netconn_close(client);
        netconn_delete(client);
        return;
    }

    AXK_LOG_DEBUG("[portal] %s %s\r\n", method, uri);

    handle_request(client, method, uri, body, (int)strlen(body));

    netconn_close(client);
    netconn_delete(client);
}

/* ==================== Portal Task ==================== */

static void portal_task(void *param)
{
    (void)param;

    /* Local watchdog device: feed each loop to prevent reset during scan */
    struct bflb_device_s *wdg = bflb_device_get_by_name("watchdog0");

    while (s_portal_running) {
        struct netconn *client;

        /* Feed watchdog every loop iteration */
        if (wdg) {
            bflb_wdg_reset_countervalue(wdg);
        }

        err_t err = netconn_accept(s_listener, &client);
        if (err != ERR_OK) {
            if (s_portal_running) {
                AXK_LOG_ERROR("[portal] accept FAIL: %d\r\n", err);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            continue;
        }

        /* Set receive timeout */
        netconn_set_recvtimeout(client, PORTAL_TIMEOUT_MS);

        handle_client(client);
    }

    /* Clean up on exit */
    if (s_listener) {
        netconn_delete(s_listener);
        s_listener = NULL;
    }
    s_portal_task = NULL;

    vTaskDelete(NULL);
}
