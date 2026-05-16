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

/* lwIP UDP RAW for captive portal DNS hijack */
#include "lwip/udp.h"
#include "lwip/pbuf.h"

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

/* Captive portal: DNS hijack to 192.168.4.1 */
#define PORTAL_DNS_PORT         53

/* HTTP 302 redirect for captive portal detection */
#define PORTAL_REDIRECT_302     "HTTP/1.1 302 Found\r\nLocation: http://192.168.4.1/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
#define PORTAL_REDIRECT_HTML    "<html><head><meta http-equiv=\"refresh\" content=\"0;url=http://192.168.4.1/\"></head></html>"

/* ==================== Internal State ==================== */

static bool s_portal_running = false;
static struct netconn *s_listener = NULL;
static TaskHandle_t s_portal_task = NULL;
static SemaphoreHandle_t s_portal_mutex = NULL;

/* Scan result cache (populated at boot and on refresh) */
static char *s_scan_json = NULL;
static bool s_scan_cached = false;   /**< true when cache has been filled at least once */
static SemaphoreHandle_t s_scan_mutex = NULL;

/* Captive portal: DNS hijack UDP PCB */
static struct udp_pcb *s_dns_pcb = NULL;

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
static int handle_save(struct netconn *client, const char *body);
static int handle_redirect(struct netconn *client);
static int handle_captive_portal(struct netconn *client, const char *uri_path);

/* Captive portal DNS hijack */
static void dns_hijack_init(void);
static void dns_hijack_deinit(void);
static void dns_hijack_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port);

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

    /* ① Auto-restore saved LLM & WiFi config from flash at boot.
     * If credentials exist, load them into runtime state so the agent
     * can use them immediately without waiting for portal config. */
    {
        char ssid[64] = {0};
        char provider[32] = {0};
        char model[64] = {0};
        char api_key[320] = {0};
        size_t len = 0;

        if (axk_kv_get_blob(KV_WIFI_SSID, ssid, sizeof(ssid), &len) == 0 && len > 0) {
            AXK_LOG_INFO("[portal] boot: found saved WiFi SSID=%s\r\n", ssid);
        }
        /* LLM config: restore to proxy runtime so axk_llm_chat_tools works */
        if (axk_kv_get_blob(KV_LLM_PROVIDER, provider, sizeof(provider), &len) == 0 && len > 0) {
            (void)axk_llm_set_provider(provider);
        }
        if (axk_kv_get_blob(KV_LLM_MODEL, model, sizeof(model), &len) == 0 && len > 0) {
            (void)axk_llm_set_model(model);
        }
        if (axk_kv_get_blob(KV_LLM_API_KEY, api_key, sizeof(api_key), &len) == 0 && len > 0) {
            (void)axk_llm_set_api_key(api_key);
        }
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

    /* Pre-scan WiFi and cache results for fast first page load.
     * AP is already running; scan may fail (single-radio), cache stays empty. */
    AXK_LOG_INFO("[portal] running pre-scan for cache...\r\n");
    portal_scan_and_cache();
    AXK_LOG_INFO("[portal] pre-scan done\r\n");

    /* Start captive portal DNS hijack (udp:53 → forged A record to 192.168.4.1) */
    dns_hijack_init();

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

    /* Stop captive portal DNS hijack */
    dns_hijack_deinit();

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
    wifi_mgmr_scan_params_t scan_params = { 0 };
    int ret;

    /*
     * Phase 1: Passive scan on AP channel (ch6) — no AP teardown.
     * On BL618 fhost single-radio, passive scan only listens for
     * beacon frames on the AP's current channel. If neighbor APs
     * share channel 6, results arrive immediately without disconnecting
     * any phone connected to the SoftAP.
     */
    scan_params.channels_cnt = 0;    /* fw uses current channel for passive */
    scan_params.passive = true;

    AXK_LOG_INFO("[portal] Phase1: passive scan (AP active, ch6)...\r\n");
    ret = wifi_mgmr_sta_scan(&scan_params);
    if (ret == 0) {
        /* Wait 3s for beacons on ch6 */
        int wait_ms = 0;
        while (wait_ms < 3000) {
            vTaskDelay(pdMS_TO_TICKS(200));
            wait_ms += 200;
            if (wifi_mgmr_sta_scanlist_nums_get() > 0) {
                AXK_LOG_INFO("[portal] Phase1 got %lu results, no AP restart needed\r\n",
                             (unsigned long)wifi_mgmr_sta_scanlist_nums_get());
                return 0;
            }
        }
        AXK_LOG_INFO("[portal] Phase1: no APs on ch6, proceeding to Phase2\r\n");
    } else {
        AXK_LOG_INFO("[portal] Phase1 passive scan unavailable (%d), Phase2 fallback\r\n", ret);
    }

    /*
     * Phase 2: Full scan — stop SoftAP, scan all channels, restart AP.
     * Brief client disconnect (~5s) is expected.
     */
    wifi_mgmr_ap_params_t ap_cfg = { 0 };
    ap_cfg.ssid = "AiPiClaw";
    ap_cfg.key = "12345678";
    {
        char saved_ssid[64] = {0};
        char saved_key[64] = {0};
        size_t len = 0;
        if (ef_get_env_blob("mimi_wifi_ssid", saved_ssid, sizeof(saved_ssid), &len) == 0 && len > 0) {
            saved_ssid[sizeof(saved_ssid) - 1] = '\0';
            ap_cfg.ssid = saved_ssid;
        }
        len = 0;
        if (ef_get_env_blob("mimi_wifi_pwd", saved_key, sizeof(saved_key), &len) == 0 && len > 0) {
            saved_key[sizeof(saved_key) - 1] = '\0';
            ap_cfg.key = saved_key;
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

    AXK_LOG_INFO("[portal] Phase2: stopping AP for full scan...\r\n");
    wifi_mgmr_ap_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    scan_params.passive = false;
    scan_params.channels_cnt = 0;
    ret = wifi_mgmr_sta_scan(&scan_params);
    if (ret != 0) {
        AXK_LOG_ERROR("[portal] Phase2 scan FAIL: %d\r\n", ret);
    } else {
        int wait_ms = 0;
        while (wait_ms < 5000) {
            vTaskDelay(pdMS_TO_TICKS(200));
            wait_ms += 200;
            if (wifi_mgmr_sta_scanlist_nums_get() > 0) break;
        }
        AXK_LOG_INFO("[portal] Phase2 got %lu results\r\n",
                     (unsigned long)wifi_mgmr_sta_scanlist_nums_get());
    }

    /* Always restart AP */
    AXK_LOG_INFO("[portal] restarting AP...\r\n");
    if (wifi_mgmr_ap_start(&ap_cfg) != 0) {
        AXK_LOG_ERROR("[portal] AP restart FAIL\r\n");
    }
    vTaskDelay(pdMS_TO_TICKS(500));

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
    xSemaphoreGive(s_scan_mutex);

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

    /* Save credentials only — DO NOT call axk_wifi_connect here.
     *
     * On BL618 single-radio fhost, wifi_mgmr_sta_connect() sends an IPC
     * command to the WiFi firmware. The IPC reply callback may fire
     * inside a context (wifi_task / Tmr Svc) where lwIP's sys_mbox_post
     * → xQueueSend triggers FreeRTOS configASSERT at queue.c:1592
     * (blocking queue op in non-scheduler context → abort).
     *
     * Instead, the save triggers axk_wifi_manager_poll() in mimi_main
     * loop (normal task context) to auto-connect on next tick. The
     * POST /api/apply endpoint handles the full AP→STA transition when
     * the user is ready. */
    AXK_LOG_INFO("[portal] POST /api/wifi/connect: ssid=%s\r\n", ssid);
    int ret = axk_wifi_save_credentials(ssid, password);
    if (ret != 0) {
        cJSON_Delete(json);
        cJSON_AddStringToObject(resp, "error", "save_failed");
        int r = send_json_response(client, 500, resp);
        cJSON_Delete(resp);
        return r;
    }

    cJSON_AddStringToObject(resp, "status", "saved");
    cJSON_AddStringToObject(resp, "ssid", ssid);

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
 * @brief POST /api/save — 统一保存WiFi+LLM配置到flash
 *
 * JSON body: { "ssid": "...", "password": "...",
 *              "provider": "...", "model": "...", "api_key": "..." }
 * 所有字段可选，只保存提供的字段。完成后不断SoftAP，用户可继续浏览。
 */
static int handle_save(struct netconn *client, const char *body)
{
    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    int n_saved = 0;
    cJSON *json = cJSON_Parse(body);
    if (!json) {
        cJSON_AddStringToObject(resp, "error", "invalid_json");
        int r = send_json_response(client, 400, resp);
        cJSON_Delete(resp);
        return r;
    }

    /* WiFi fields */
    cJSON *ssid_j = cJSON_GetObjectItem(json, "ssid");
    cJSON *pass_j = cJSON_GetObjectItem(json, "password");
    if (ssid_j && cJSON_IsString(ssid_j) && ssid_j->valuestring[0]) {
        const char *pwd = (pass_j && cJSON_IsString(pass_j)) ? pass_j->valuestring : "";
        if (axk_wifi_save_credentials(ssid_j->valuestring, pwd) == 0) {
            n_saved++;
        }
    }

    /* LLM fields */
    cJSON *provider_j = cJSON_GetObjectItem(json, "provider");
    if (provider_j && cJSON_IsString(provider_j)) {
        if (axk_llm_set_provider(provider_j->valuestring) == 0) n_saved++;
    }
    cJSON *model_j = cJSON_GetObjectItem(json, "model");
    if (model_j && cJSON_IsString(model_j)) {
        if (axk_llm_set_model(model_j->valuestring) == 0) n_saved++;
    }
    cJSON *api_key_j = cJSON_GetObjectItem(json, "api_key");
    if (api_key_j && cJSON_IsString(api_key_j) && api_key_j->valuestring[0]) {
        if (axk_llm_set_api_key(api_key_j->valuestring) == 0) n_saved++;
    }

    cJSON_Delete(json);

    if (n_saved > 0) {
        cJSON_AddStringToObject(resp, "status", "ok");
        cJSON_AddNumberToObject(resp, "saved", n_saved);
    } else {
        cJSON_AddStringToObject(resp, "status", "no_data");
    }

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief POST /api/apply — 保存配置 → 停AP → 连STA → 转聊天
 *
 * 这是三步骤配置的最终确认按钮后端。统一保存WiFi+LLM到flash，
 * 然后停止SoftAP + config portal，连接目标WiFi，重定向到/chat。
 */
static int handle_apply(struct netconn *client, const char *body)
{
    (void)body;

    cJSON *resp = cJSON_CreateObject();
    if (!resp) {
        return send_response(client, 500, "application/json",
                            "{\"error\":\"OOM\"}");
    }

    /* TEST mode: save only, no AP→STA transition.
     * Normal mode would stop AP+portal and connect STA here. */
    ef_save_env();

    AXK_LOG_INFO("[portal] POST /api/apply: config saved (TEST mode, AP kept alive)\r\n");

    cJSON_AddStringToObject(resp, "status", "applied");
    cJSON_AddStringToObject(resp, "wifi_connecting", "test_mode");
    cJSON_AddStringToObject(resp, "ws_port", "18789");
    cJSON_AddStringToObject(resp, "chat_url", "/chat");
    cJSON_AddStringToObject(resp, "apply_msg",
                            "Configuration saved (TEST mode).");

    int r = send_json_response(client, 200, resp);
    cJSON_Delete(resp);
    return r;
}

/**
 * @brief Captive portal detection + non-API path handler.
 *
 * Detects known captive portal probing URLs and returns the expected
 * response so iOS/Android auto-pop the config portal:
 *   - iOS:     GET /hotspot-detect.html → 200 Success
 *   - Android: GET /generate_204        → 204 No Content
 *   - Other:   302 redirect to http://192.168.4.1/
 */
static int handle_captive_portal(struct netconn *client, const char *uri_path)
{
    /* iOS captive.apple.com: /hotspot-detect.html → 200 Success */
    if (strcmp(uri_path, "/hotspot-detect.html") == 0 ||
        strcmp(uri_path, "/library/test/success.html") == 0) {
        return send_response(client, 200, "text/html",
            "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    }

    /* Android connectivitycheck: /generate_204 → 204 No Content */
    if (strcmp(uri_path, "/generate_204") == 0) {
        return send_response(client, 204, "text/plain", "");
    }

    /* All other non-API paths: return 200 Success.
     * iOS/Android treat 200 as "portal detected, show login page".
     * Some versions ignore 302 redirects for captive portal detection. */
    return send_response(client, 200, "text/html",
        "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
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

    /* POST /api/save — 统一保存WiFi+LLM */
    if (strcmp(uri_path, "/api/save") == 0) {
        if (strcmp(method, "POST") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_save(client, body);
    }

    /* POST /api/apply — 完成配置，停AP转STA */
    if (strcmp(uri_path, "/api/apply") == 0) {
        if (strcmp(method, "POST") != 0) {
            return send_response(client, 405, "text/plain", "Method Not Allowed");
        }
        return handle_apply(client, body);
    }

    /* Captive portal: detect known probing URLs, return 200 for unknown */
    return handle_captive_portal(client, uri_path);
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

    AXK_LOG_INFO("[portal] %s %s\r\n", method, uri);

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

/* ==================== Captive Portal DNS Hijack ==================== */

/**
 * @brief DNS hijack receive callback — forged A-record to 192.168.4.1.
 *
 * In lwIP NO_SYS mode, udp_recv callbacks fire inside the tcpip_thread
 * context. udp_sendto from here is safe ONLY during SoftAP-only mode
 * (no STA connect → no fhost IPC → netif->output is non-blocking).
 * On a STA-connected system, revert this to log-only to avoid
 * tcpip_thread blocking on fhost IPC.
 *
 * iOS/Android captive portal detection first resolves the detection
 * domain via DNS. If DNS returns an IP we own (and port 80 responds),
 * the OS connects via HTTP and gets our detection response.
 */
static void dns_hijack_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                            const ip_addr_t *addr, u16_t port)
{
    (void)arg;

    if (!p || p->len < 12) {
        if (p) pbuf_free(p);
        return;
    }

    uint8_t *dns = (uint8_t *)p->payload;
    uint16_t flags = (dns[2] << 8) | dns[3];
    if ((flags & 0xF800) != 0x0000) {  /* not a standard query */
        pbuf_free(p);
        return;
    }

    /* Reuse TXID from query */
    uint16_t txid = (dns[0] << 8) | dns[1];

    /* Build minimal forged response: A record → 192.168.4.1 */
    uint8_t resp[128];
    int off = 0;
    resp[off++] = (txid >> 8) & 0xFF;
    resp[off++] = txid & 0xFF;
    resp[off++] = 0x85;  /* QR|AA|RD */
    resp[off++] = 0x80;  /* RA */
    resp[off++] = 0x00; resp[off++] = 0x01;  /* QDCOUNT=1 */
    resp[off++] = 0x00; resp[off++] = 0x01;  /* ANCOUNT=1 */
    resp[off++] = 0x00; resp[off++] = 0x00;  /* NSCOUNT=0 */
    resp[off++] = 0x00; resp[off++] = 0x00;  /* ARCOUNT=0 */

    /* Copy original question */
    int qlen = p->len - 12;
    if (qlen > 96) qlen = 96;
    if (off + qlen + 16 > (int)sizeof(resp)) {
        pbuf_free(p);
        return;
    }
    memcpy(resp + off, dns + 12, qlen);
    off += qlen;

    /* Answer: pointer to query name, type A, class IN, TTL=60, 192.168.4.1 */
    resp[off++] = 0xC0; resp[off++] = 0x0C;
    resp[off++] = 0x00; resp[off++] = 0x01;  /* A */
    resp[off++] = 0x00; resp[off++] = 0x01;  /* IN */
    resp[off++] = 0x00; resp[off++] = 0x00; resp[off++] = 0x00; resp[off++] = 0x3C; /* TTL=60 */
    resp[off++] = 0x00; resp[off++] = 0x04;  /* RDLENGTH=4 */
    resp[off++] = 192; resp[off++] = 168; resp[off++] = 4; resp[off++] = 1;

    struct pbuf *resp_p = pbuf_alloc(PBUF_TRANSPORT, off, PBUF_RAM);
    if (resp_p) {
        memcpy(resp_p->payload, resp, off);
        udp_sendto(pcb, resp_p, addr, port);
        pbuf_free(resp_p);
    }
    pbuf_free(p);
}

/**
 * @brief Start DNS hijack: bind UDP port 53 on AP IP.
 */
static void dns_hijack_init(void)
{
    if (s_dns_pcb) return;

    s_dns_pcb = udp_new();
    if (!s_dns_pcb) {
        AXK_LOG_ERROR("[portal] DNS hijack: udp_new FAIL\r\n");
        return;
    }

    ip_addr_t ap_ip;
    IP4_ADDR(&ap_ip, 192, 168, 4, 1);
    err_t err = udp_bind(s_dns_pcb, &ap_ip, PORTAL_DNS_PORT);
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[portal] DNS hijack: udp_bind port 53 FAIL: %d\r\n", err);
        udp_remove(s_dns_pcb);
        s_dns_pcb = NULL;
        return;
    }

    udp_recv(s_dns_pcb, dns_hijack_recv, NULL);
    AXK_LOG_INFO("[portal] DNS hijack started on 192.168.4.1:53\r\n");
}

/**
 * @brief Stop DNS hijack.
 */
static void dns_hijack_deinit(void)
{
    if (s_dns_pcb) {
        udp_remove(s_dns_pcb);
        s_dns_pcb = NULL;
        AXK_LOG_INFO("[portal] DNS hijack stopped\r\n");
    }
}
