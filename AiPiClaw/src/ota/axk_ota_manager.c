/**
 * @file axk_ota_manager.c
 * @brief OTA \u7ba1\u7406\u5668\u5b9e\u73b0 - \u57fa\u4e8e SDK HTTPS FOTA
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 \u5b89\u4fe1\u53ef\u79d1\u6280\u6709\u9650\u516c\u53f8
 * @note \u63d0\u4f9bHTTP/HTTPS OTA\u5347\u7ea7\u80fd\u529b\uff0c\u652f\u6301\u56de\u6eda
 */

#include "axk_ota_manager.h"
#include "axk_platform.h"
#include "https_fota.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

static const char *TAG = "ota";

static bool s_ota_in_progress = false;
static SemaphoreHandle_t s_ota_mutex = NULL;

/**
 * @brief FOTA\u72b6\u6001\u56de\u8c03\u51fd\u6570
 *
 * @param[in] arg \u7528\u6237\u53c2\u6570
 * @param[in] event FOTA\u72b6\u6001\u4e8b\u4ef6
 */
static void axk_ota_status_callback(void *arg, https_fota_status_t event)
{
    (void)arg;

    switch (event) {
    case HTTPS_FOTA_START:
        AXK_LOG_INFO("[%s] OTA\u5f00\u59cb\r\n", TAG);
        break;
    case HTTPS_FOTA_SERVER_CONNECTE_FAIL:
        AXK_LOG_ERROR("[%s] \u670d\u52a1\u5668\u8fde\u63a5\u5931\u8d25\r\n", TAG);
        break;
    case HTTPS_FOTA_PROCESS_TRANSFER:
        AXK_LOG_INFO("[%s] \u56fa\u4ef6\u4f20\u8f93\u4e2d...\r\n", TAG);
        break;
    case HTTPS_FOTA_TRANSFER_FINISH:
        AXK_LOG_INFO("[%s] \u56fa\u4ef6\u4f20\u8f93\u5b8c\u6210\r\n", TAG);
        break;
    case HTTPS_FOTA_IMAGE_VERIFY:
        AXK_LOG_INFO("[%s] \u56fa\u4ef6\u9a8c\u8bc1\u4e2d...\r\n", TAG);
        break;
    case HTTPS_FOTA_IMAGE_VERIFY_FAIL:
        AXK_LOG_ERROR("[%s] \u56fa\u4ef6\u9a8c\u8bc1\u5931\u8d25\r\n", TAG);
        break;
    case HTTPS_FOTA_ABORT:
        AXK_LOG_WARN("[%s] OTA\u5df2\u4e2d\u6b62\r\n", TAG);
        break;
    default:
        AXK_LOG_INFO("[%s] OTA\u72b6\u6001: %d\r\n", TAG, (int)event);
        break;
    }
}

/**
 * @brief TODO: 描述axk_ota_manager_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_ota_manager_init(void)
{
    s_ota_mutex = xSemaphoreCreateMutex();
    if (!s_ota_mutex) {
        AXK_LOG_ERROR("[%s] \u521b\u5efa\u4e92\u65a5\u9501\u5931\u8d25\r\n", TAG);
        return -1;
    }

    s_ota_in_progress = false;
    AXK_LOG_INFO("[%s] OTA\u7ba1\u7406\u5668\u521d\u59cb\u5316\u5b8c\u6210\r\n", TAG);
    return 0;
}

/**
 * @brief \u68c0\u67e5OTA\u662f\u5426\u6b63\u5728\u8fdb\u884c
 *
 * @return true\u8868\u793aOTA\u4e2d
 */
bool axk_ota_is_in_progress(void)
{
    bool in_progress;

    if (xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return true; /* \u65e0\u6cd5\u83b7\u53d6\u9501\uff0c\u5047\u8bbe\u6b63\u5728\u8fdb\u884c\u4e2d */
    }
    in_progress = s_ota_in_progress;
    xSemaphoreGive(s_ota_mutex);
    return in_progress;
}

/**
 * @brief \u542f\u52a8OTA\u4e0b\u8f7d\u4efb\u52a1
 *
 * @param[in] url \u56fa\u4ef6\u4e0b\u8f7dURL
 * @return \u6210\u529f\u8fd4\u56de0\uff0c\u5931\u8d25\u8fd4\u56de\u975e\u96f6
 */
int axk_ota_start(const char *url)
{
    struct https_fota_config config = {0};
    int ret;

    if (!url || url[0] == '\0') {
        AXK_LOG_ERROR("[%s] URL\u4e3a\u7a7a\r\n", TAG);
        return -1;
    }

    if (xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        AXK_LOG_WARN("[%s] \u65e0\u6cd5\u83b7\u53d6OTA\u9501\r\n", TAG);
        return -1;
    }

    if (s_ota_in_progress) {
        AXK_LOG_WARN("[%s] OTA\u5df2\u5728\u8fdb\u884c\u4e2d\r\n", TAG);
        xSemaphoreGive(s_ota_mutex);
        return -1;
    }

    s_ota_in_progress = true;
    xSemaphoreGive(s_ota_mutex);

    AXK_LOG_INFO("[%s] \u5f00\u59cbOTA\u4e0b\u8f7d: %s\r\n", TAG, url);

    config.callback = axk_ota_status_callback;
    config.user_arg = NULL;

    ret = https_fota(url, &config);

    if (xSemaphoreTake(s_ota_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        s_ota_in_progress = false;
        xSemaphoreGive(s_ota_mutex);
    }

    if (ret != 0) {
        AXK_LOG_ERROR("[%s] OTA\u5931\u8d25: %d\r\n", TAG, ret);
        return -1;
    }

    AXK_LOG_INFO("[%s] OTA\u5b8c\u6210\uff0c\u8bf7\u91cd\u542f\u8bbe\u5907\r\n", TAG);
    return 0;
}

/**
 * @brief \u56de\u6eda\u5230\u5907\u4efd\u56fa\u4ef6
 *
 * @return \u6210\u529f\u8fd4\u56de0\uff0c\u5931\u8d25\u8fd4\u56de\u975e\u96f6
 */
int axk_ota_rollback(void)
{
    int ret = https_ota_rollback();
    if (ret == 0) {
        AXK_LOG_INFO("[%s] \u56de\u6eda\u6807\u8bb0\u5df2\u5199\u5165\uff0c\u8bf7\u91cd\u542f\u8bbe\u5907\r\n", TAG);
    } else {
        AXK_LOG_ERROR("[%s] \u56de\u6eda\u5931\u8d25: %d\r\n", TAG, ret);
    }
    return ret;
}
