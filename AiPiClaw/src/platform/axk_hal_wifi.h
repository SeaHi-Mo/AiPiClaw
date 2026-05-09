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
    char ssid[AXK_WIFI_MAX_SSID_LEN + 1];
    char password[AXK_WIFI_MAX_PASS_LEN + 1];
    int8_t rssi;
    uint8_t channel;
    uint8_t bssid[6];
} axk_wifi_ap_info_t;

typedef void (*axk_wifi_event_cb_t)(axk_wifi_state_t state, void* arg);

int axk_hal_wifi_init(axk_wifi_mode_t mode);
int axk_hal_wifi_connect(const char* ssid, const char* password);
int axk_hal_wifi_disconnect(void);
int axk_hal_wifi_scan(axk_wifi_ap_info_t* ap_list, uint32_t max_count, uint32_t* out_count);
axk_wifi_state_t axk_hal_wifi_get_state(void);
int axk_hal_wifi_register_event_cb(axk_wifi_event_cb_t cb, void* arg);
const char* axk_hal_wifi_get_ip(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_WIFI_H */
