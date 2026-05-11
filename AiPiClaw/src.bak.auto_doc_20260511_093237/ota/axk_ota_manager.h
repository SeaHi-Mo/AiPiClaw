/**
 * @file axk_ota_manager.h
 * @brief ota_manager module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_OTA_MANAGER_H
#define __AXK_OTA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_ota_manager_init(void);
bool axk_ota_is_in_progress(void);
int axk_ota_start(const char *url);
int axk_ota_rollback(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_OTA_MANAGER_H */
