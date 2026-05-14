/**
 * @file axk_tool_board_info.c
 * @brief board_info 工具 - LLM 查询板卡和系统信息
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 返回芯片、开发板、固件版本、GPIO别名列表、LED极性、运行时间、WiFi SSID、可用堆内存
 */

#include "axk_tool_board_info.h"
#include "axk_board_config.h"
#include "axk_platform.h"
#include "axk_tool_registry.h"
#include "axk_wifi_manager.h"

#include "cJSON.h"
#include "bflb_rtc.h"
#include "FreeRTOS.h"

#include <stdio.h>
#include <string.h>

static const char *TAG __attribute__((unused)) = "tool_board_info";

void axk_tool_board_info_register(void)
{
    mimi_tool_t t = {
        .name = "board_info",
        .description = "Get board hardware info: chip model, board name, firmware version, "
                       "available GPIO aliases, LED polarity, uptime, WiFi SSID, free heap memory.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = axk_tool_board_info_execute,
    };
    axk_register_tool(&t);
}

int axk_tool_board_info_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json; /* no input parameters required */

    if (!output || output_size == 0) return -1;

    const axk_board_config_t *cfg = axk_board_config_get();

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        snprintf(output, output_size, "{\"error\":\"memory allocation failed\"}");
        return -1;
    }

    /* ── 板卡基本信息 ── */
    if (cfg) {
        cJSON_AddStringToObject(root, "board_name", cfg->board_name);
        cJSON_AddStringToObject(root, "chip_model", cfg->chip_model);
        cJSON_AddNumberToObject(root, "chip_freq_mhz", cfg->chip_freq_mhz);
        cJSON_AddNumberToObject(root, "sram_kb", cfg->sram_kb);
        cJSON_AddNumberToObject(root, "psram_mb", cfg->psram_mb);
        cJSON_AddNumberToObject(root, "flash_mb", cfg->flash_mb);
        cJSON_AddStringToObject(root, "sdk_version", cfg->sdk_version);
        cJSON_AddStringToObject(root, "firmware_version", cfg->fw_version);
        cJSON_AddStringToObject(root, "led_polarity", cfg->led_polarity);
    } else {
        cJSON_AddStringToObject(root, "board_name", "unknown");
        cJSON_AddStringToObject(root, "chip_model", "unknown");
    }

    /* ── GPIO 别名列表 ── */
    cJSON *alias_arr = cJSON_CreateArray();
    axk_board_alias_entry_t aliases[AXK_BOARD_MAX_ALIASES];
    int alias_count = axk_board_config_get_aliases(aliases, AXK_BOARD_MAX_ALIASES);
    for (int i = 0; i < alias_count; i++) {
        cJSON *a = cJSON_CreateObject();
        cJSON_AddStringToObject(a, "name", aliases[i].name);
        cJSON_AddNumberToObject(a, "pin", aliases[i].pin);
        cJSON_AddStringToObject(a, "rw",
            (aliases[i].flags & 1) ? ((aliases[i].flags & 2) ? "rw" : "w") :
            ((aliases[i].flags & 2) ? "r" : "-"));
        /* 回读当前电平（如果可读） */
        if (aliases[i].flags & 2) {
            int raw = axk_hal_gpio_get_level(aliases[i].pin);
            int on = (raw == aliases[i].active_level) ? 1 : 0;
            cJSON_AddStringToObject(a, "state", on ? "on" : "off");
        }
        cJSON_AddStringToObject(a, "desc", aliases[i].desc);
        cJSON_AddItemToArray(alias_arr, a);
    }
    cJSON_AddItemToObject(root, "gpio_aliases", alias_arr);

    /* ── 运行时间 ── */
    {
        uint32_t tick = xTaskGetTickCount();
        uint32_t uptime_s = tick / configTICK_RATE_HZ;
        cJSON_AddNumberToObject(root, "uptime_seconds", uptime_s);
    }

    /* ── WiFi SSID ── */
    {
        char ssid[64] = { 0 };
        if (axk_wifi_is_connected()) {
            axk_wifi_get_ssid(ssid, sizeof(ssid));
        }
        cJSON_AddStringToObject(root, "wifi_ssid", ssid[0] ? ssid : "(not connected)");
    }

    /* ── 可用堆内存 ── */
    {
        size_t free_heap = xPortGetFreeHeapSize();
        cJSON_AddNumberToObject(root, "free_heap_bytes", (double)free_heap);
    }

    /* ── 输出 ── */
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        snprintf(output, output_size, "{\"error\":\"json serialization failed\"}");
        return -1;
    }

    snprintf(output, output_size, "%s", json_str);
    free(json_str);

    return 0;
}
