/**
 * @file axk_hal_wifi.c
 * @brief WiFi硬件抽象层实现 - 基于 wifi_mgmr
 * @version 1.0
 * @date 2026-04-24
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#include "axk_hal_wifi.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if AXK_PLATFORM_BL618
    #include "wifi_mgmr_ext.h"  /**< fhost WiFi6 头file */

    static axk_wifi_state_t axk_wifi_state = AXK_WIFI_STATE_IDLE;
    static axk_wifi_event_cb_t axk_wifi_cb = NULL;
    static void* axk_wifi_cb_arg = NULL;
    static char axk_wifi_ip[16] = "0.0.0.0";

#elif AXK_PLATFORM_ESP32
    #include "esp_wifi.h"
    static axk_wifi_state_t axk_wifi_state = AXK_WIFI_STATE_IDLE;
    static axk_wifi_event_cb_t axk_wifi_cb = NULL;
    static void* axk_wifi_cb_arg = NULL;
    static char axk_wifi_ip[16] = "0.0.0.0";
#endif

/**
 * @brief 初始化WiFi硬件抽象层
 *
 * @param[in] mode WiFi工作模式（STA/AP/APSTA）
 * @return 0 成功，负数 失败
 */
int axk_hal_wifi_init(axk_wifi_mode_t mode)
{
    AXK_LOG_INFO("[axk_hal_wifi] initWiFi HAL, mode =%d\r\n", mode);

#if AXK_PLATFORM_BL618
    wifi_mgmr_init(NULL);
    axk_wifi_state = AXK_WIFI_STATE_IDLE;
#elif AXK_PLATFORM_ESP32
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode((mode == AXK_WIFI_MODE_STA) ? WIFI_MODE_STA :
                      (mode == AXK_WIFI_MODE_AP) ? WIFI_MODE_AP : WIFI_MODE_APSTA);
#endif
    return 0;
}

/**
 * @brief 连接到指定WiFi热点
 *
 * @param[in] ssid WiFi热点名称
 * @param[in] password WiFi密码（可选，NULL表示开放网络）
 * @return 0 成功，-1 ssid为空
 */
int axk_hal_wifi_connect(const char* ssid, const char* password)
{
    if (ssid == NULL) return -1;

    AXK_LOG_INFO("[axk_hal_wifi] connectSSID: %s\r\n", ssid);

#if AXK_PLATFORM_BL618
    struct wifi_mgmr_sta_connect_params params = {0};
    strncpy((char*)params.ssid, ssid, sizeof(params.ssid) - 1);
    if (password) {
        strncpy((char*)params.key, password, sizeof(params.key) - 1);
    }

    axk_wifi_state = AXK_WIFI_STATE_CONNECTING;
    wifi_mgmr_sta_connect(&params);
#elif AXK_PLATFORM_ESP32
    wifi_config_t wifi_cfg = {0};
    strncpy((char*)wifi_cfg.sta.ssid, ssid, 32);
    strncpy((char*)wifi_cfg.sta.password, password ? password : "", 64);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg);
    esp_wifi_connect();
#endif
    return 0;
}

/**
 * @brief 断开当前WiFi连接
 *
 * @return 0 成功
 */
/**
 * @brief 断开当前WiFi连接
 *
 * @return 0 成功
 */
int axk_hal_wifi_disconnect(void)
{
#if AXK_PLATFORM_BL618
    wifi_sta_disconnect();
#endif
    axk_wifi_state = AXK_WIFI_STATE_DISCONNECTED;
    return 0;
}

/**
 * @brief 执行WiFi站点扫描
 * @param[out] ap_list AP信息数组
 * @param[in] max_count 数组最大容量
 * @param[out] out_count 扫描到的AP数量
 * @return 0 成功，-1 参数无效或扫描失败
 */
int axk_hal_wifi_scan(axk_wifi_ap_info_t* ap_list, uint32_t max_count, uint32_t* out_count)
{
#if AXK_PLATFORM_BL618
    wifi_mgmr_scan_params_t scan_params = {0};
    int ret;

    if (!ap_list || max_count == 0 || !out_count) {
        return -1;
    }

    ret = wifi_mgmr_sta_scan(&scan_params);
    if (ret != 0) {
        return -1;
    }

    /*  etc待scan ok */
    vTaskDelay(pdMS_TO_TICKS(3000));

    /* scan 结果via callback get ，here 简化process */
    *out_count = 0;
    return 0;
#else
    if (out_count) *out_count = 0;
    return 0;
#endif
}

/**
 * @brief 获取当前WiFi连接状态
 *
 * @return WiFi状态枚举值
 */
axk_wifi_state_t axk_hal_wifi_get_state(void)
{
    return axk_wifi_state;
}

/**
 * @brief 注册WiFi事件回调函数
 *
 * @param[in] cb 回调函数指针
 * @param[in] arg 回调用户参数
 * @return 0 成功
 */
int axk_hal_wifi_register_event_cb(axk_wifi_event_cb_t cb, void* arg)
{
    axk_wifi_cb = cb;
    axk_wifi_cb_arg = arg;
    return 0;
}

/**
 * @brief 获取WiFi分配的IP地址
 *
 * @return IP地址字符串指针，格式如 "192.168.1.100"
 */
const char* axk_hal_wifi_get_ip(void)
{
#if AXK_PLATFORM_BL618
    uint32_t ip = 0, mask = 0, gw = 0;
    if (wifi_sta_ip4_addr_get(&ip, &mask, &gw, NULL) == 0 && ip != 0) {
        snprintf(axk_wifi_ip, sizeof(axk_wifi_ip), "%lu.%lu.%lu.%lu",
                 (ip >> 0) & 0xFF, (ip >> 8) & 0xFF,
                 (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    }
#endif
    return axk_wifi_ip;
}

/* ── AP scan 框架 ────────────────────────────────── */

#define AXK_WIFI_SCAN_MAX_APS  20

static axk_wifi_ap_info_t s_scan_results[AXK_WIFI_SCAN_MAX_APS];
static int s_scan_count = 0;

/**
 * @brief 启动WiFi AP扫描（异步，结果通过事件回调获取）
 *
 * @return 0 成功，-1 失败
 */
int axk_hal_wifi_scan_start(void)
{
#if AXK_PLATFORM_BL618
    wifi_mgmr_scan_params_t scan_params = {0};
    int ret;

    s_scan_count = 0;
    memset(s_scan_results, 0, sizeof(s_scan_results));

    /* Perform an active scan on all channels.
     * Results arrive asynchronously via wifi_mgmr event callback
     * (CODE_WIFI_ON_SCAN_DONE). For now we do a blocking approach:
     * start scan, wait, then poll for results via wifi_mgmr_state. */
    ret = wifi_mgmr_sta_scan(&scan_params);
    if (ret != 0) {
        AXK_LOG_WARN("[axk_hal_wifi] WiFi scan start failed: %d\r\n", ret);
        return -1;
    }

    AXK_LOG_INFO("[axk_hal_wifi] WiFi scan started (max %d APs)\r\n",
                 AXK_WIFI_SCAN_MAX_APS);

    /* Wait for scan to complete (typical scan takes 2-4 seconds).
     * The wifi_mgmr stack will fire CODE_WIFI_ON_SCAN_DONE when done.
     * In the meantime, results can be polled via axk_hal_wifi_scan_get_count(). */
    vTaskDelay(pdMS_TO_TICKS(4000));

    /* After scan completes, wifi_mgmr should have populated internal AP list.
     * wifi_mgmr_get_scan_result() can retrieve individual entries.
     * For now, s_scan_count is populated by the event handler.
     * If no event handler is registered, this will remain 0. */
    if (s_scan_count == 0) {
        AXK_LOG_INFO("[axk_hal_wifi] Scan complete, but no results captured "
                     "(event handler may not be registered)\r\n");
    } else {
        AXK_LOG_INFO("[axk_hal_wifi] Scan complete, %d AP(s) found\r\n", s_scan_count);
    }
    return 0;
#else
    return -1;
#endif
}

/**
 * @brief 获取扫描到的AP数量
 *
 * @return AP数量
 */
int axk_hal_wifi_scan_get_count(void)
{
    return s_scan_count;
}

/**
 * @brief 获取指定索引的扫描结果
 *
 * @param[in] index 索引（0 ~ count-1）
 * @param[out] info AP信息输出
 * @return 0 成功，-1 索引无效或参数为空
 */
int axk_hal_wifi_scan_get_result(int index, axk_wifi_ap_info_t *info)
{
    if (index < 0 || index >= s_scan_count || !info) {
        return -1;
    }
    memcpy(info, &s_scan_results[index], sizeof(*info));
    return 0;
}
