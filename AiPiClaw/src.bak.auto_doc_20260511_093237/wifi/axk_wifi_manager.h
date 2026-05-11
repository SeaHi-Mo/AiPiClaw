/**
 * @file axk_wifi_manager.h
 * @brief WiFimanager头file - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-23
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 基于 Bouffalo SDK fhost (WiFi6) 实现
 */

#ifndef __AXK_WIFI_MANAGER_H
#define __AXK_WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFiconnectstatus 枚举
 */
typedef enum {
    AXK_WIFI_STATE_DISCONNECTED = 0,   /**< not connect */
    AXK_WIFI_STATE_CONNECTING,         /**< 正in connect */
    AXK_WIFI_STATE_CONNECTED,          /**< connect to APbut not get IP */
    AXK_WIFI_STATE_GOT_IP,             /**< get IP，网络ready  */
} axk_wifi_state_t;

/**
 * @brief WiFi事件callback func type 
 * @param[in] state current WiFistatus 
 * @param[in] user_data user自定义data
 */
typedef void (*axk_wifi_event_cb_t)(axk_wifi_state_t state, void *user_data);

/**
 * @brief initWiFimanager
 * @return OKreturn 0，FAILreturn 非零
 * @note need 先okWiFi硬件init（rfparam_init, tcpip_init, wifi_task_create, fhost_init）
 */
int axk_wifi_manager_init(void);

/**
 * @brief poll WiFi事件（connectstatus 、断线reconnect  etc）
 * @note in main loop定期call 
 */
void axk_wifi_manager_poll(void);

/**
 * @brief connect指定WiFi热点
 * @param[in] ssid WiFi SSID
 * @param[in] password WiFipassword （开放网络传NULL）
 * @return OKreturn 0，FAILreturn 非零
 * @note 该func 为异步操作，connect结果via 事件notify 
 */
int axk_wifi_connect(const char *ssid, const char *password);

/**
 * @brief disconnectcurrent WiFiconnect
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_disconnect(void);

/**
 * @brief get current WiFiconnectstatus 
 * @return current status枚举value 
 */
axk_wifi_state_t axk_wifi_get_state(void);

/**
 * @brief check WiFiis否connect to AP and get IP
 * @return true表示网络ready ，false表示not ready 
 */
bool axk_wifi_is_connected(void);

/**
 * @brief get current IPaddr chars 串
 * @param[out] buf output buffer 
 * @param[in] buf_size buffer size
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_get_ip(char *buf, size_t buf_size);

/**
 * @brief get current RSSI信号强度
 * @param[out] rssi RSSIvalue （dBm）
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_get_rssi(int *rssi);

/**
 * @brief get current connectSSID
 * @param[out] buf output buffer 
 * @param[in] buf_size buffer size
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_get_ssid(char *buf, size_t buf_size);

/**
 * @brief registerWiFistatus 变化callback 
 * @param[in] cb callback func 
 * @param[in] user_data user自定义data
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_register_event_cb(axk_wifi_event_cb_t cb, void *user_data);

/**
 * @brief unreg WiFistatus 变化callback 
 * @param[in] cb callback func 
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_wifi_unregister_event_cb(axk_wifi_event_cb_t cb);

/**
 * @brief set WiFiauto reconnect enabled
 * @param[in] enable trueenabled，falsedisable 
 */
void axk_wifi_set_auto_reconnect(bool enable);

/**
 * @brief get WiFiauto reconnect status 
 * @return true表示auto reconnect enabled
 */
bool axk_wifi_get_auto_reconnect(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_WIFI_MANAGER_H */
