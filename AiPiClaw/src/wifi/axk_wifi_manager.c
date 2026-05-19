/**
 * @file axk_wifi_manager.c
 * @brief WiFimanager实现 - 基于 Bouffalo SDK fhost (WiFi6)
 * @version 1.0
 * @date 2026-04-23
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note provide STAmode 下connect、disconnect、status 查询、auto reconnect  etc功能
 */

#include "axk_wifi_manager.h"
#include "axk_mimiclaw.h"
#include "axk_message_bus.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* Bouffalo SDK WiFi6 (fhost) 头file */
#include "wifi_mgmr_ext.h"
#include "wifi_mgmr.h"
#include "async_event.h"

/* KV storage for credential persistence */
#include "axk_storage.h"

/* lwIP 头file，for IPaddr convert  */
#include "lwip/ip_addr.h"

/* ============== private data结构 ============== */

#define AXK_WIFI_SSID_MAX_LEN      32
#define AXK_WIFI_PASSWORD_MAX_LEN  64
#define AXK_WIFI_MAX_CB_NUM        4
#define AXK_WIFI_RECONNECT_DELAY_MS 5000
#define AXK_WIFI_MAX_RETRY         10
#define AXK_WIFI_BACKOFF_BASE_MS   1000

/**
 * @brief Calculate exponential backoff: 1s, 2s, 4s, 8s, 16s, 32s (capped at 32s)
 */
static inline uint32_t axk_wifi_backoff_ms(int retry_count)
{
    uint32_t delay = AXK_WIFI_BACKOFF_BASE_MS << retry_count; /* 1 << n */
    if (delay > 32000) delay = 32000;
    return delay;
}

/**
 * @brief WiFimanager上下文结构
 */
typedef struct {
    axk_wifi_state_t state;                         /**< current WiFistatus */
    char ssid[AXK_WIFI_SSID_MAX_LEN + 1];           /**< save SSID */
    char password[AXK_WIFI_PASSWORD_MAX_LEN + 1];   /**< save password */
    bool auto_reconnect;                            /**< auto reconnect enabled */
    bool pending_reconnect;                         /**< 待执行reconnect */
    TickType_t reconnect_tick;                        /**< reconnect timer (raw FreeRTOS ticks) */
    int retry_count;                                /**< current reconnect attempts counter */
    uint32_t reconnect_delay_ms;                    /**< current backoff delay for this reconnect cycle */
    bool max_retry_exceeded;                        /**< true when retry_count >= AXK_WIFI_MAX_RETRY */

    axk_wifi_event_cb_t cbs[AXK_WIFI_MAX_CB_NUM];   /**< registercallback func 数组 */
    void *cb_user_data[AXK_WIFI_MAX_CB_NUM];        /**< callback userdata */

    SemaphoreHandle_t mutex;                        /**< mutex ，保护shared data */
} axk_wifi_manager_ctx_t;

static axk_wifi_manager_ctx_t g_wifi_ctx;

/* ============== internalhelper func  ============== */

/**
 * @brief get current FreeRTOS tick count
 * @note All time comparisons MUST use subtraction pattern (now - stored_tick)
 *       which is safe with unsigned wraparound. Direct >/< comparison with
 *       absolute stored tick values will break after 49.7 days.
 */
static inline TickType_t axk_wifi_get_tick(void)
{
    return xTaskGetTickCount();
}

/**
 * @brief notify allregistercallback func status 变化
 *
 * @param[in] state 新WiFistatus 
 */
static void axk_wifi_notify_callbacks(axk_wifi_state_t state)
{
    for (int i = 0; i < AXK_WIFI_MAX_CB_NUM; i++) {
        if (g_wifi_ctx.cbs[i] != NULL) {
            g_wifi_ctx.cbs[i](state, g_wifi_ctx.cb_user_data[i]);
        }
    }
}

/**
 * @brief update WiFistatus ， and trigger callback 
 *
 * @param[in] new_state 新status 
 */
static void axk_wifi_set_state(axk_wifi_state_t new_state)
{
    if (g_wifi_ctx.state == new_state) {
        return;
    }

    AXK_LOG_INFO("[axk_wifi_manager] status 变化: %d -> %d\r\n", g_wifi_ctx.state, new_state);
    g_wifi_ctx.state = new_state;
    axk_wifi_notify_callbacks(new_state);
}

/**
 * @brief WiFi异步事件processfunc 
 *
 * @param[in] event 事件结构体
 * @param[in] private_data private data
 * @note 该func 由async事件systemcall 
 */
static void axk_wifi_event_handler(async_input_event_t event, void *private_data)
{
    (void)private_data;

    if (event == NULL) {
        return;
    }

    switch (event->code) {
        case CODE_WIFI_ON_INIT_DONE:
            AXK_LOG_INFO("[axk_wifi_manager] WiFi硬件initok\r\n");
            break;

        case CODE_WIFI_ON_CONNECTING:
            AXK_LOG_INFO("[axk_wifi_manager] 正in connectWiFi...\r\n");
            axk_wifi_set_state(AXK_WIFI_STATE_CONNECTING);
            break;

        case CODE_WIFI_ON_CONNECTED:
            AXK_LOG_INFO("[axk_wifi_manager] connect to AP\r\n");
            /* async_event callback may fire in wifi_task/IPC context
             * where blocking xSemaphoreTake triggers queue.c assert.
             * Use trylock (0 timeout) to avoid crash. */
            if (xSemaphoreTake(g_wifi_ctx.mutex, 0) == pdTRUE) {
                g_wifi_ctx.retry_count = 0;
                g_wifi_ctx.max_retry_exceeded = false;
                g_wifi_ctx.pending_reconnect = false;
                xSemaphoreGive(g_wifi_ctx.mutex);
            }
            axk_wifi_set_state(AXK_WIFI_STATE_CONNECTED);
            break;

        case CODE_WIFI_ON_GOT_IP:
            AXK_LOG_INFO("[axk_wifi_manager] get IPaddr \r\n");
            /* async_event callback — trylock only to avoid queue assert */
            if (xSemaphoreTake(g_wifi_ctx.mutex, 0) == pdTRUE) {
                g_wifi_ctx.retry_count = 0;
                g_wifi_ctx.max_retry_exceeded = false;
                g_wifi_ctx.pending_reconnect = false;
                xSemaphoreGive(g_wifi_ctx.mutex);
            }
            axk_wifi_set_state(AXK_WIFI_STATE_GOT_IP);
            break;

        case CODE_WIFI_ON_DISCONNECT:
            AXK_LOG_WARN("[axk_wifi_manager] WiFiconnectdisconnect\r\n");
            /* async_event callback — trylock only to avoid queue assert */
            if (xSemaphoreTake(g_wifi_ctx.mutex, 0) == pdTRUE) {
                if (g_wifi_ctx.auto_reconnect && g_wifi_ctx.ssid[0] != '\0') {
                    g_wifi_ctx.retry_count++;
                    /* Copy ssid to local before Give to avoid TOCTOU */
                    char local_ssid[sizeof(g_wifi_ctx.ssid)];
                    strncpy(local_ssid, g_wifi_ctx.ssid, sizeof(local_ssid) - 1);
                    local_ssid[sizeof(local_ssid) - 1] = '\0';
                    if (g_wifi_ctx.retry_count >= AXK_WIFI_MAX_RETRY) {
                        AXK_LOG_ERROR("[axk_wifi_manager] max retry (%d) reached, stop reconnecting\r\n",
                                      AXK_WIFI_MAX_RETRY);
                        g_wifi_ctx.max_retry_exceeded = true;
                        g_wifi_ctx.pending_reconnect = false;
                        xSemaphoreGive(g_wifi_ctx.mutex);
                        /* Notify via message bus */
                        mimi_msg_t msg = {0};
                        strncpy(msg.channel, MIMI_CHAN_SYSTEM, sizeof(msg.channel) - 1);
                        char buf[128];
                        snprintf(buf, sizeof(buf),
                                 "WiFi retry exhausted: SSID=%s retries=%d",
                                 local_ssid, AXK_WIFI_MAX_RETRY);
                        msg.content = strdup(buf);
                        if (msg.content) {
                            msg.priority = MIMI_PRIO_HIGH;
                            msg.is_error = true;
                            axk_message_bus_push_outbound(&msg);
                            free(msg.content);
                        }
                    } else {
                        uint32_t delay_ms = axk_wifi_backoff_ms(g_wifi_ctx.retry_count - 1);
                        g_wifi_ctx.pending_reconnect = true;
                        g_wifi_ctx.reconnect_tick = axk_wifi_get_tick();
                        g_wifi_ctx.reconnect_delay_ms = delay_ms;
                        xSemaphoreGive(g_wifi_ctx.mutex);
                        AXK_LOG_INFO("[axk_wifi_manager] reconnect attempt %d/%d after %lums\r\n",
                                     g_wifi_ctx.retry_count, AXK_WIFI_MAX_RETRY,
                                     (unsigned long)delay_ms);
                    }
                } else {
                    xSemaphoreGive(g_wifi_ctx.mutex);
                }
            } else {
                AXK_LOG_WARN("[axk_wifi_manager] DISCONNECT mutex TAKE FAIL, skip reconnect handling\r\n");
            }
            axk_wifi_set_state(AXK_WIFI_STATE_DISCONNECTED);
            break;

        case CODE_WIFI_ON_GOT_IP_TIMEOUT:
            AXK_LOG_WARN("[axk_wifi_manager] get IPtimeout\r\n");
            break;

        case CODE_WIFI_ON_SCAN_DONE:
            AXK_LOG_INFO("[axk_wifi_manager] WiFiscan ok\r\n");
            break;

        case CODE_WIFI_ON_PRE_GOT_IP:
            AXK_LOG_INFO("[axk_wifi_manager] 正in get IP...\r\n");
            break;

        case CODE_WIFI_ON_LOST_IP:
            AXK_LOG_WARN("[axk_wifi_manager] IPaddr 丢失\r\n");
            break;

        default:
            AXK_LOG_DEBUG("[axk_wifi_manager] not processWiFi事件: code=%lu\r\n", event->code);
            break;
    }
}

/* ============== externalAPI实现 ============== */

/**
 * @brief 初始化WiFi管理器
 *
 * @return 0成功, -1失败
 */
int axk_wifi_manager_init(void)
{
    memset(&g_wifi_ctx, 0, sizeof(g_wifi_ctx));
    g_wifi_ctx.state = AXK_WIFI_STATE_DISCONNECTED;
    g_wifi_ctx.auto_reconnect = true;
    g_wifi_ctx.mutex = xSemaphoreCreateMutex();

    if (g_wifi_ctx.mutex == NULL) {
        AXK_LOG_ERROR("[axk_wifi_manager] createmutex FAIL\r\n");
        return -1;
    }

    /* registerWiFi事件callback ，listen allWiFi事件 */
    int ret = async_register_event_filter(EV_WIFI, axk_wifi_event_handler, NULL);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_manager] registerWiFi事件过滤器FAIL: %d\r\n", ret);
        vSemaphoreDelete(g_wifi_ctx.mutex);
        g_wifi_ctx.mutex = NULL;
        return -1;
    }

    /* enabledSDK内建auto reconnect 机制 */
    wifi_mgmr_sta_autoconnect_enable();

    AXK_LOG_INFO("[axk_wifi_manager] WiFimanagerinitok\r\n");
    return 0;
}

/**
 * @brief 轮询WiFi事件（连接状态、断线重连等）
 *
 */
void axk_wifi_manager_poll(void)
{
    if (g_wifi_ctx.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }

    /* processauto reconnect with exponential backoff */
    if (g_wifi_ctx.pending_reconnect) {
        TickType_t elapsed = axk_wifi_get_tick() - g_wifi_ctx.reconnect_tick;
        if (elapsed >= pdMS_TO_TICKS(g_wifi_ctx.reconnect_delay_ms)) {
            g_wifi_ctx.pending_reconnect = false;
            uint32_t delay_used = g_wifi_ctx.reconnect_delay_ms;
            xSemaphoreGive(g_wifi_ctx.mutex);

            AXK_LOG_INFO("[axk_wifi_manager] attempt auto reconnect #%d after %lums: SSID=%s\r\n",
                         g_wifi_ctx.retry_count, (unsigned long)delay_used, g_wifi_ctx.ssid);
            int ret = axk_wifi_connect(g_wifi_ctx.ssid, g_wifi_ctx.password);
            if (ret != 0) {
                AXK_LOG_ERROR("[axk_wifi_manager] auto reconnect FAIL, will retry via next DISCONNECT event\r\n");
            }
            return; /* 经release mutex，直接return  */
        }
    }

    xSemaphoreGive(g_wifi_ctx.mutex);
}

/**
 * @brief 连接指定WiFi热点
 *
 * @param[in] ssid WiFi SSID
 * @param[in] password WiFi密码（开放网络传NULL）
 * @return 0成功, -1失败
 */
int axk_wifi_connect(const char *ssid, const char *password)
{
    if (ssid == NULL || ssid[0] == '\0') {
        AXK_LOG_ERROR("[axk_wifi_manager] SSIDnot 能为empty \r\n");
        return -1;
    }

    size_t ssid_len = strlen(ssid);
    if (ssid_len > AXK_WIFI_SSID_MAX_LEN) {
        AXK_LOG_ERROR("[axk_wifi_manager] SSID过长（最大%dbytes）\r\n", AXK_WIFI_SSID_MAX_LEN);
        return -1;
    }

    const char *pwd = password ? password : "";
    size_t pwd_len = strlen(pwd);
    if (pwd_len > AXK_WIFI_PASSWORD_MAX_LEN) {
        AXK_LOG_ERROR("[axk_wifi_manager] password 过长（最大%dbytes）\r\n", AXK_WIFI_PASSWORD_MAX_LEN);
        return -1;
    }

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        AXK_LOG_ERROR("[axk_wifi_manager] get mutex timeout\r\n");
        return -1;
    }

    /* save SSID & password for auto reconnect  */
    memcpy(g_wifi_ctx.ssid, ssid, ssid_len + 1);
    memcpy(g_wifi_ctx.password, pwd, pwd_len + 1);

    /* build connectparam
     * NOTE: static 生命周期！SDK wifi_mgmr_sta_connect() 内部仅存指针，
     *      DHCP 异步任务 (fhost_wpa_connected_task) 执行时若 params 已
     *      出栈 → 读取垃圾数据 (如 use_dhcp) → instruction access fault。
     *      memset 清零确保每次新连前无旧数据残留。 */
    static wifi_mgmr_sta_connect_params_t params;
    memset(&params, 0, sizeof(params));
    memcpy(params.ssid, ssid, ssid_len);
    params.ssid_len = (uint8_t)ssid_len;

    if (pwd_len > 0) {
        memcpy(params.key, pwd, pwd_len);
        params.key_len = (uint8_t)pwd_len;
    }

    params.use_dhcp = 1;  /* use DHCPget IP */
    params.scan_mode = 0; /* in all频道scan  */

    AXK_LOG_INFO("[axk_wifi_manager] attempt connectWiFi: SSID=%s\r\n", ssid);

    int ret = wifi_mgmr_sta_connect(&params);
    xSemaphoreGive(g_wifi_ctx.mutex);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_manager] call wifi_mgmr_sta_connectFAIL: %d\r\n", ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 触发立即重连（延迟=0，用于 portal apply 后的 STA 连接）
 *
 * 从 flash 读取已保存的 SSID/PASSWORD 填入 g_wifi_ctx，
 * 然后标记 pending_reconnect，由主循环 axk_wifi_manager_poll() 在
 * 安全的任务上下文中执行实际连接。
 *
 * @note 不直接调 wifi_mgmr_sta_connect（避免 IPC 上下文 queue.c crash）
 */
void axk_wifi_trigger_reconnect(void)
{
    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        AXK_LOG_ERROR("[axk_wifi_manager] trigger_reconnect: get mutex timeout\r\n");
        return;
    }

    /* Read saved credentials from flash into g_wifi_ctx */
    char ssid[AXK_WIFI_SSID_MAX_LEN + 1] = {0};
    char pwd[AXK_WIFI_PASSWORD_MAX_LEN + 1] = {0};
    size_t len = 0;
    bool has_ssid = false;

    if (axk_kv_get_blob("mimi_wifi_ssid", ssid, sizeof(ssid), &len) == 0 && len > 0) {
        ssid[sizeof(ssid) - 1] = '\0';
        memcpy(g_wifi_ctx.ssid, ssid, sizeof(g_wifi_ctx.ssid));
        has_ssid = true;
    }
    len = 0;
    if (axk_kv_get_blob("mimi_wifi_pwd", pwd, sizeof(pwd), &len) == 0 && len > 0) {
        pwd[sizeof(pwd) - 1] = '\0';
        memcpy(g_wifi_ctx.password, pwd, sizeof(g_wifi_ctx.password));
    }

    if (!has_ssid) {
        AXK_LOG_ERROR("[axk_wifi_manager] trigger_reconnect: no saved SSID in flash\r\n");
        xSemaphoreGive(g_wifi_ctx.mutex);
        return;
    }

    g_wifi_ctx.retry_count = 0;
    g_wifi_ctx.max_retry_exceeded = false;
    g_wifi_ctx.reconnect_delay_ms = 0;    /* immediate */
    g_wifi_ctx.reconnect_tick = axk_wifi_get_tick();
    g_wifi_ctx.pending_reconnect = true;

    xSemaphoreGive(g_wifi_ctx.mutex);

    AXK_LOG_INFO("[axk_wifi_manager] pending_reconnect set for SSID=%s\r\n", ssid);
}

/**
 * @brief 断开当前WiFi连接
 *
 * @return 0成功, -1失败
 */
int axk_wifi_disconnect(void)
{
    AXK_LOG_INFO("[axk_wifi_manager] disconnectWiFiconnect\r\n");

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    g_wifi_ctx.pending_reconnect = false;

    int ret = wifi_sta_disconnect();
    xSemaphoreGive(g_wifi_ctx.mutex);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_manager] disconnectconnectFAIL: %d\r\n", ret);
        return -1;
    }

    return 0;
}

/**
 * @brief 获取当前WiFi连接状态
 *
 * @return 当前状态枚举值
 */
axk_wifi_state_t axk_wifi_get_state(void)
{
    axk_wifi_state_t state;

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return AXK_WIFI_STATE_DISCONNECTED;
    }

    state = g_wifi_ctx.state;
    xSemaphoreGive(g_wifi_ctx.mutex);

    return state;
}

/**
 * @brief 检查WiFi是否已连接AP并获取IP
 *
 * @return true表示网络就绪，false表示未就绪
 */
bool axk_wifi_is_connected(void)
{
    return (axk_wifi_get_state() == AXK_WIFI_STATE_GOT_IP);
}

/**
 * @brief 获取当前IP地址字符串
 *
 * @param[out] buf 输出缓冲区
 * @param[in] buf_size 缓冲区大小
 * @return 0成功, -1失败
 */
int axk_wifi_get_ip(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size < 16) {
        return -1;
    }

    uint32_t addr = 0, mask = 0, gw = 0, dns = 0;
    int ret = wifi_sta_ip4_addr_get(&addr, &mask, &gw, &dns);
    if (ret != 0 || addr == 0) {
        buf[0] = '\0';
        return -1;
    }

    ip4_addr_t ip4;
    ip4.addr = addr;
    snprintf(buf, buf_size, "%s", ip4addr_ntoa(&ip4));

    return 0;
}

/**
 * @brief 获取当前RSSI信号强度
 *
 * @param[out] rssi RSSI值（dBm）
 * @return 0成功, -1失败
 */
int axk_wifi_get_rssi(int *rssi)
{
    if (rssi == NULL) {
        return -1;
    }

    int ret = wifi_mgmr_sta_rssi_get(rssi);
    if (ret != 0) {
        return -1;
    }

    return 0;
}

/**
 * @brief 获取当前连接SSID
 *
 * @param[out] buf 输出缓冲区
 * @param[in] buf_size 缓冲区大小
 * @return 0成功, -1失败
 */
int axk_wifi_get_ssid(char *buf, size_t buf_size)
{
    if (buf == NULL || buf_size == 0) {
        return -1;
    }

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return -1;
    }

    if (g_wifi_ctx.ssid[0] == '\0') {
        xSemaphoreGive(g_wifi_ctx.mutex);
        buf[0] = '\0';
        return -1;
    }

    strncpy(buf, g_wifi_ctx.ssid, buf_size - 1);
    buf[buf_size - 1] = '\0';
    xSemaphoreGive(g_wifi_ctx.mutex);

    return 0;
}

/**
 * @brief 注册WiFi状态变化回调
 *
 * @param[in] cb 回调函数
 * @param[in] user_data 用户自定义数据
 * @return 0成功, -1失败
 */
int axk_wifi_register_event_cb(axk_wifi_event_cb_t cb, void *user_data)
{
    if (cb == NULL) {
        return -1;
    }

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    for (int i = 0; i < AXK_WIFI_MAX_CB_NUM; i++) {
        if (g_wifi_ctx.cbs[i] == NULL) {
            g_wifi_ctx.cbs[i] = cb;
            g_wifi_ctx.cb_user_data[i] = user_data;
            xSemaphoreGive(g_wifi_ctx.mutex);
            return 0;
        }
    }

    xSemaphoreGive(g_wifi_ctx.mutex);
    AXK_LOG_ERROR("[axk_wifi_manager] callback func 数组full \r\n");
    return -1;
}

/**
 * @brief 注销WiFi状态变化回调
 *
 * @param[in] cb 回调函数
 * @return 0成功, -1失败
 */
int axk_wifi_unregister_event_cb(axk_wifi_event_cb_t cb)
{
    if (cb == NULL) {
        return -1;
    }

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    for (int i = 0; i < AXK_WIFI_MAX_CB_NUM; i++) {
        if (g_wifi_ctx.cbs[i] == cb) {
            g_wifi_ctx.cbs[i] = NULL;
            g_wifi_ctx.cb_user_data[i] = NULL;
            xSemaphoreGive(g_wifi_ctx.mutex);
            return 0;
        }
    }

    xSemaphoreGive(g_wifi_ctx.mutex);
    return -1;
}

/**
 * @brief 设置WiFi自动重连开关
 *
 * @param[in] enable true启用，false禁用
 */
void axk_wifi_set_auto_reconnect(bool enable)
{
    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    g_wifi_ctx.auto_reconnect = enable;

    if (enable) {
        wifi_mgmr_sta_autoconnect_enable();
    } else {
        wifi_mgmr_sta_autoconnect_disable();
    }

    xSemaphoreGive(g_wifi_ctx.mutex);

    AXK_LOG_INFO("[axk_wifi_manager] auto reconnect %s\r\n", enable ? "enabled" : "disable ");
}

/**
 * @brief 获取WiFi自动重连状态
 *
 * @return true表示自动重连已启用
 */
bool axk_wifi_get_auto_reconnect(void)
{
    bool enable;

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    enable = g_wifi_ctx.auto_reconnect;
    xSemaphoreGive(g_wifi_ctx.mutex);

    return enable;
}

/**
 * @brief 检测WiFi重连是否已达到最大重试次数
 *
 * @return true 表示已超过最大重试次数
 */
bool axk_wifi_is_max_retry_exceeded(void)
{
    bool exceeded;

    if (xSemaphoreTake(g_wifi_ctx.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }

    exceeded = g_wifi_ctx.max_retry_exceeded;
    xSemaphoreGive(g_wifi_ctx.mutex);

    return exceeded;
}
