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

#ifdef __cplusplus
extern "C" {
#endif

int axk_wifi_onboard_init(void);
void axk_wifi_onboard_poll(void);
int axk_wifi_auto_connect(void);
int axk_wifi_onboard_start(void);
void axk_wifi_onboard_stop(void);

/**
 * @brief save WiFicredential  to easyflash
 * @param[in] ssid SSIDchars 串
 * @param[in] password password chars 串（可为NULL or empty chars 串）
 * @return OKreturn 0
 */
int axk_wifi_save_credentials(const char *ssid, const char *password);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_WIFI_ONBOARD_H */
