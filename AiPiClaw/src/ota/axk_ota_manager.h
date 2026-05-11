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

/* @brief TODO: 描述axk_ota_manager_init的功能 @return 0成功, -1失败 */
int axk_ota_manager_init(void);
/* @brief TODO: 描述axk_ota_is_in_progress的功能 @return 0成功, -1失败 */
bool axk_ota_is_in_progress(void);
/* @brief TODO: 描述axk_ota_start的功能 @param url TODO: 描述url @return 0成功, -1失败 */
int axk_ota_start(const char *url);
/* @brief TODO: 描述axk_ota_rollback的功能 @return 0成功, -1失败 */
int axk_ota_rollback(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_OTA_MANAGER_H */
