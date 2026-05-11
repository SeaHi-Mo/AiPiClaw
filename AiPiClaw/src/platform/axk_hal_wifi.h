/**
 * @file axk_hal_wifi.h
 * @brief WiFi硬件抽象层 - 宏元编程实现平台解耦
 * @version 1.0
 * @date 2026-04-20
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_WIFI_H
#define __AXK_HAL_WIFI_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

#define AXK_WIFI_MAX_SSID_LEN   32
#define AXK_WIFI_MAX_PASS_LEN   64

typedef enum {
    AXK_WIFI_MODE_NONE = 0,
    AXK_WIFI_MODE_STA,
    AXK_WIFI_MODE_AP,
    AXK_WIFI_MODE_APSTA
} axk_wifi_mode_t;

typedef enum {
    AXK_WIFI_STATE_IDLE = 0,
    AXK_WIFI_STATE_CONNECTING,
    AXK_WIFI_STATE_CONNECTED,
    AXK_WIFI_STATE_DISCONNECTED,
    AXK_WIFI_STATE_FAILED
} axk_wifi_state_t;

typedef struct {
    char ssid[AXK_WIFI_MAX_SSID_LEN + 1];       /**< WiFi热点名称 */
    char password[AXK_WIFI_MAX_PASS_LEN + 1];   /**< WiFi密码 */
    int8_t rssi;                                 /**< 信号强度(dBm) */
    uint8_t channel;                             /**< 信道号 */
    uint8_t bssid[6];                            /**< AP的BSSID(MAC地址) */
} axk_wifi_ap_info_t;

typedef void (*axk_wifi_event_cb_t)(axk_wifi_state_t state, void* arg);  /**< WiFi事件回调函数类型 */

/**
 * @brief 初始化WiFi HAL层
 * @param[in] mode WiFi工作模式
 * @return 0成功，负数错误码
 */
int axk_hal_wifi_init(axk_wifi_mode_t mode);
/**
 * @brief 连接到WiFi热点
 * @param[in] ssid WiFi名称
 * @param[in] password WiFi密码
 * @return 0成功，负数错误码
 */
int axk_hal_wifi_connect(const char* ssid, const char* password);
/**
 * @brief 断开WiFi连接
 * @return 0成功
 */
int axk_hal_wifi_disconnect(void);
/**
 * @brief 扫描WiFi热点
 * @param[out] ap_list AP信息数组
 * @param[in] max_count 最大AP数
 * @param[out] out_count 实际扫描到的AP数
 * @return 0成功
 */
int axk_hal_wifi_scan(axk_wifi_ap_info_t* ap_list, uint32_t max_count, uint32_t* out_count);
/**
 * @brief 获取WiFi连接状态
 * @return 当前WiFi状态枚举值
 */
axk_wifi_state_t axk_hal_wifi_get_state(void);
/**
 * @brief 注册WiFi事件回调
 * @param[in] cb 回调函数
 * @param[in] arg 用户参数
 * @return 0成功
 */
int axk_hal_wifi_register_event_cb(axk_wifi_event_cb_t cb, void* arg);
/**
 * @brief 获取WiFi连接后的IP地址
 * @return IP地址字符串
 */
const char* axk_hal_wifi_get_ip(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_WIFI_H */
