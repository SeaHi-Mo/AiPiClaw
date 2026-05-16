/**
 * @file axk_wifi_onboard.h
 * @brief wifi_onboard module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_WIFI_ONBOARD_H
#define __AXK_WIFI_ONBOARD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TODO: 描述axk_wifi_onboard_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_wifi_onboard_init(void);
/**
 * @brief TODO: 描述axk_wifi_onboard_poll的功能
 *
 * @return 无返回值
 */
void axk_wifi_onboard_poll(void);
/**
 * @brief TODO: 描述axk_wifi_auto_connect的功能
 *
 * @return 0成功, -1失败
 */
int axk_wifi_auto_connect(void);
/**
 * @brief TODO: 描述axk_wifi_onboard_start的功能
 *
 * @return 0成功, -1失败
 */
int axk_wifi_onboard_start(void);
/**
 * @brief TODO: 描述axk_wifi_onboard_stop的功能
 *
 * @return 无返回值
 */
void axk_wifi_onboard_stop(void);

/**
 * @brief 查询是否处于onboard配网模式（SoftAP运行中）
 *
 * @return true SoftAP正在运行，false 未运行
 */
bool axk_wifi_onboard_is_active(void);
/**
 * @brief save WiFicredential  to easyflash
 *
 * @param[in] ssid SSIDchars 串
 * @param[in] password password chars 串（可为NULL or empty chars 串）
 * @return OKreturn 0
 */
int axk_wifi_save_credentials(const char *ssid, const char *password);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_WIFI_ONBOARD_H */
