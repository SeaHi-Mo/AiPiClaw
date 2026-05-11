/**
 * @file axk_tool_registry.c
 * @brief tool registry实现 - mgrallbuiltin tool
 * @version 1.0
 * @date 2026-04-23
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_tool_registry.h"
#include "mimi_config.h"
#include "axk_platform.h"

#include "axk_tool_web_search.h"
#include "axk_tool_get_time.h"
#include "axk_tool_files.h"
#include "axk_tool_gpio.h"
#include "axk_tool_cron.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "cJSON.h"

#define MAX_TOOLS 16

static mimi_tool_t s_tools[MAX_TOOLS];
static int s_tool_count = 0;
static char *s_tools_json = NULL;  /* 缓存JSON数组chars 串 */

static void axk_register_tool(const mimi_tool_t *tool)
{
    if (s_tool_count >= MAX_TOOLS) {
        AXK_LOG_ERROR("[axk_tool_registry] tool registryfull \r\n");
        return;
    }
    s_tools[s_tool_count++] = *tool;
    AXK_LOG_INFO("[axk_tool_registry] registeredtool: %s\r\n", tool->name);
}

static void axk_build_tools_json(void)
{
    cJSON *arr = cJSON_CreateArray();
    int i;

    for (i = 0; i < s_tool_count; i++) {
        cJSON *tool = cJSON_CreateObject();
        cJSON_AddStringToObject(tool, "name", s_tools[i].name);
        cJSON_AddStringToObject(tool, "description", s_tools[i].description);

        cJSON *schema = cJSON_Parse(s_tools[i].input_schema_json);
        if (schema) {
            cJSON_AddItemToObject(tool, "input_schema", schema);
        }

        cJSON_AddItemToArray(arr, tool);
    }

    if (s_tools_json) {
        free(s_tools_json);
    }
    s_tools_json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    AXK_LOG_INFO("[axk_tool_registry] toolJSONbuilt (%d tool)\r\n", s_tool_count);
}

int axk_tool_registry_init(void)
{
    s_tool_count = 0;

    /* register web_search tool */
    axk_tool_web_search_init();
    {
        mimi_tool_t ws = {
            .name = "web_search",
            .description = "Search the web for current information via Tavily (preferred) or Brave when configured.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"query\":{\"type\":\"string\",\"description\":\"搜索关键词\"}},"
                "\"required\":[\"query\"]}",
            .execute = axk_tool_web_search_execute,
        };
        axk_register_tool(&ws);
    }

    /* register get_current_time tool */
    {
        mimi_tool_t gt = {
            .name = "get_current_time",
            .description = "Get the current date and time. Also sets the system clock. Call this when you need to know what time or date it is.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = axk_tool_get_time_execute,
        };
        axk_register_tool(&gt);
    }

    /* registerfilerelated tool */
    {
        mimi_tool_t rf = {
            .name = "read_file",
            .description = "Read a file from storage. Path must start with /storage/.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"绝对path ，以 /storage/ 开头\"}},"
                "\"required\":[\"path\"]}",
            .execute = axk_tool_read_file_execute,
        };
        axk_register_tool(&rf);
    }

    {
        mimi_tool_t wf = {
            .name = "write_file",
            .description = "Write or overwrite a file on storage. Path must start with /storage/.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"绝对path ，以 /storage/ 开头\"},"
                "\"content\":{\"type\":\"string\",\"description\":\"要writefilecontent \"}},"
                "\"required\":[\"path\",\"content\"]}",
            .execute = axk_tool_write_file_execute,
        };
        axk_register_tool(&wf);
    }

    {
        mimi_tool_t ef = {
            .name = "edit_file",
            .description = "Find and replace text in a file. Replaces first occurrence of old_string with new_string.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"绝对path \"},"
                "\"old_string\":{\"type\":\"string\",\"description\":\"要find 文本\"},"
                "\"new_string\":{\"type\":\"string\",\"description\":\"replace 文本\"}},"
                "\"required\":[\"path\",\"old_string\",\"new_string\"]}",
            .execute = axk_tool_edit_file_execute,
        };
        axk_register_tool(&ef);
    }

    {
        mimi_tool_t ld = {
            .name = "list_dir",
            .description = "List files on storage, optionally filtered by path prefix.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"prefix\":{\"type\":\"string\",\"description\":\"可选path 前缀过滤器\"}},"
                "\"required\":[]}",
            .execute = axk_tool_list_dir_execute,
        };
        axk_register_tool(&ld);
    }

    /* register GPIO tool */
    axk_tool_gpio_init();
    {
        mimi_tool_t gw = {
            .name = "gpio_write",
            .description = "Set a GPIO pin HIGH or LOW. Controls LEDs, relays, and other digital outputs.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIOpin 编号\"},"
                "\"state\":{\"type\":\"integer\",\"description\":\"1=HIGH, 0=LOW\"}},"
                "\"required\":[\"pin\",\"state\"]}",
            .execute = axk_tool_gpio_write_execute,
        };
        axk_register_tool(&gw);
    }

    {
        mimi_tool_t gr = {
            .name = "gpio_read",
            .description = "Read a GPIO pin state. Returns HIGH or LOW.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"pin\":{\"type\":\"integer\",\"description\":\"GPIOpin 编号\"}},"
                "\"required\":[\"pin\"]}",
            .execute = axk_tool_gpio_read_execute,
        };
        axk_register_tool(&gr);
    }

    {
        mimi_tool_t ga = {
            .name = "gpio_read_all",
            .description = "Read all allowed GPIO pin states in a single call.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = axk_tool_gpio_read_all_execute,
        };
        axk_register_tool(&ga);
    }

    /* registercrontasktool */
    {
        mimi_tool_t ca = {
            .name = "cron_add",
            .description = "Schedule a recurring or one-shot task. The message will trigger an agent turn when the job fires.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{"
                "\"name\":{\"type\":\"string\",\"description\":\"Short name for the job\"},"
                "\"schedule_type\":{\"type\":\"string\",\"description\":\"'every' for recurring interval or 'at' for one-shot at a unix timestamp\"},"
                "\"interval_s\":{\"type\":\"integer\",\"description\":\"Interval in seconds (required for 'every')\"},"
                "\"at_epoch\":{\"type\":\"integer\",\"description\":\"Unix timestamp to fire at (required for 'at')\"},"
                "\"message\":{\"type\":\"string\",\"description\":\"Message to inject when the job fires, triggering an agent turn\"},"
                "\"channel\":{\"type\":\"string\",\"description\":\"Optional reply channel (e.g. 'telegram'). If omitted, current turn channel is used when available\"},"
                "\"chat_id\":{\"type\":\"string\",\"description\":\"Optional reply chat_id. Required when channel='telegram'. If omitted during a Telegram turn, current chat_id is used\"}"
                "},"
                "\"required\":[\"name\",\"schedule_type\",\"message\"]}",
            .execute = axk_tool_cron_add_execute,
        };
        axk_register_tool(&ca);
    }

    {
        mimi_tool_t cl = {
            .name = "cron_list",
            .description = "List all scheduled cron jobs with their status, schedule, and IDs.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{},"
                "\"required\":[]}",
            .execute = axk_tool_cron_list_execute,
        };
        axk_register_tool(&cl);
    }

    {
        mimi_tool_t cr = {
            .name = "cron_remove",
            .description = "Remove a scheduled cron job by its ID.",
            .input_schema_json =
                "{\"type\":\"object\","
                "\"properties\":{\"job_id\":{\"type\":\"string\",\"description\":\"The 8-character job ID to remove\"}},"
                "\"required\":[\"job_id\"]}",
            .execute = axk_tool_cron_remove_execute,
        };
        axk_register_tool(&cr);
    }

    axk_build_tools_json();

    AXK_LOG_INFO("[axk_tool_registry] tool registryinitok\r\n");
    return 0;
}

const char *axk_tool_registry_get_tools_json(void)
{
    return s_tools_json;
}

int axk_tool_registry_execute(const char *name, const char *input_json,
                              char *output, size_t output_size)
{
    int i;

    for (i = 0; i < s_tool_count; i++) {
        if (strcmp(s_tools[i].name, name) == 0) {
            AXK_LOG_INFO("[axk_tool_registry] 执行tool: %s\r\n", name);
            return s_tools[i].execute(input_json, output, output_size);
        }
    }

    AXK_LOG_WARN("[axk_tool_registry] not 知tool: %s\r\n", name);
    snprintf(output, output_size, "Error: unknown tool '%s'", name);
    return -1;
}
