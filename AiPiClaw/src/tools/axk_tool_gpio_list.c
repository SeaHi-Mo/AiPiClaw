/**
 * @file axk_tool_gpio_list.c
 * @brief gpio_list_aliases 工具 - LLM 列出所有GPIO别名和实时状态
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 返回 [{name, pin, rw, state, desc}, ...]
 */

#include "axk_tool_gpio_list.h"
#include "axk_board_config.h"
#include "axk_platform.h"
#include "axk_tool_registry.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

static const char *TAG __attribute__((unused)) = "tool_gpio_list";

void axk_tool_gpio_list_register(void)
{
    mimi_tool_t t = {
        .name = "gpio_list_aliases",
        .description = "List all available GPIO aliases with current pin states. "
                       "Use this to discover which pins are available before operating them.",
        .input_schema_json =
            "{\"type\":\"object\","
            "\"properties\":{},"
            "\"required\":[]}",
        .execute = axk_tool_gpio_list_execute,
    };
    axk_register_tool(&t);
}

int axk_tool_gpio_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    if (!output || output_size == 0) return -1;

    cJSON *arr = cJSON_CreateArray();
    if (!arr) {
        snprintf(output, output_size, "[]");
        return -1;
    }

    axk_board_alias_entry_t aliases[AXK_BOARD_MAX_ALIASES];
    int count = axk_board_config_get_aliases(aliases, AXK_BOARD_MAX_ALIASES);

    for (int i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", aliases[i].name);
        cJSON_AddNumberToObject(item, "pin", aliases[i].pin);

        /* rw 字符串 */
        const char *rw;
        if ((aliases[i].flags & 1) && (aliases[i].flags & 2)) {
            rw = "rw";
        } else if (aliases[i].flags & 1) {
            rw = "w";
        } else if (aliases[i].flags & 2) {
            rw = "r";
        } else {
            rw = "-";
        }
        cJSON_AddStringToObject(item, "rw", rw);

        /* 读取当前电平（如果可读） */
        if (aliases[i].flags & 2) {
            int raw = axk_hal_gpio_get_level(aliases[i].pin);
            int on = (raw == aliases[i].active_level) ? 1 : 0;
            cJSON_AddStringToObject(item, "state", on ? "on" : "off");
        } else {
            cJSON_AddStringToObject(item, "state", "unknown");
        }

        cJSON_AddStringToObject(item, "desc", aliases[i].desc);
        cJSON_AddItemToArray(arr, item);
    }

    char *json_str = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (!json_str) {
        snprintf(output, output_size, "[]");
        return -1;
    }

    snprintf(output, output_size, "%s", json_str);
    free(json_str);

    return 0;
}
