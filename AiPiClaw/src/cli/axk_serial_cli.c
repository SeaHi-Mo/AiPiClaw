/**
 * @file axk_serial_cli.c
 * @brief serial CLI - 基于 BL618 SDK Shell 组件
 * @version 2.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 集成 SDK shell，provide  mimi / status / wifi / sys / gpio  etc诊断cmd
 *       param parse support 引号chars 串 & 转义
 */

#include "axk_serial_cli.h"
#include "axk_platform.h"
#include "axk_message_bus.h"
#include "axk_hal_system.h"
#include "axk_hal_gpio.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "semphr.h"
#include "shell.h"

#define AXK_CLI_MAX_INPUT_LEN  512
#define AXK_CLI_PROMPT         "mimi> "

static SemaphoreHandle_t s_cli_mutex = NULL;

/* ── Shell cmd: mimi ───────────────────────────── */

/**
 * @brief mimi <msg> - sendmsg to  AI Agent
 */
static int cmd_mimi(int argc, char **argv)
{
    char msg_buf[AXK_CLI_MAX_INPUT_LEN];
    int i;
    size_t pos = 0;

    if (argc < 2) {
        printf("Usage: mimi <msg>\r\n  e.g.: mimi What time is it?\r\n");
        return 0;
    }

    for (i = 1; i < argc; i++) {
        size_t arg_len = strlen(argv[i]);
        if (pos + arg_len + 1 >= sizeof(msg_buf)) break;
        if (i > 1) msg_buf[pos++] = ' ';
        memcpy(msg_buf + pos, argv[i], arg_len);
        pos += arg_len;
    }
    msg_buf[pos] = '\0';

    mimi_msg_t msg = {0};
    strncpy(msg.channel, MIMI_CHAN_CLI, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, "cli_user", sizeof(msg.chat_id) - 1);
    msg.content = msg_buf;
    msg.priority = MIMI_PRIO_NORMAL;

    if (axk_message_bus_push_inbound(&msg) != 0) {
        printf("[!] Message queue full, please retry later\r\n");
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_mimi, mimi, mimi <msg> - send message to AI Agent);

/* ── Shell cmd: status ──────────────────────────── */

/**
 * @brief TODO: 描述cmd_status的功能
 *
 * @param argc TODO: 描述argc
 * @param argv TODO: 描述argv
 * @return 0成功, -1失败
 */
static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;

    uint32_t free_heap = axk_hal_system_get_free_heap();
    const char *chip = axk_hal_system_get_chip_name();
    const char *sdk  = axk_hal_system_get_sdk_version();
    uint32_t uptime = axk_hal_system_get_time_ms() / 1000;

    printf("=== MimiClaw BL618 Status ===\r\n");
    printf("  Chip:      %s\r\n", chip ? chip : "?");
    printf("  SDK:       %s\r\n", sdk  ? sdk  : "?");
    printf("  Uptime:    %lus\r\n", (unsigned long)uptime);
    printf("  Free Heap: %lu KB\r\n", (unsigned long)(free_heap / 1024));
    printf("  CLI:       OK\r\n");
    printf("=============================\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_status, status, status - show system status);

/* ── Shell cmd: sys ─────────────────────────────── */

/**
 * @brief TODO: 描述cmd_sys的功能
 *
 * @param argc TODO: 描述argc
 * @param argv TODO: 描述argv
 * @return 0成功, -1失败
 */
static int cmd_sys(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: sys <reset|info|heap>\r\n");
        return 0;
    }

    if (strcmp(argv[1], "reset") == 0) {
        printf("System will reset in 1 second...\r\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
        axk_hal_system_reset();
    } else if (strcmp(argv[1], "info") == 0) {
        cmd_status(0, NULL);
    } else if (strcmp(argv[1], "heap") == 0) {
        printf("Free heap: %lu bytes\r\n",
               (unsigned long)axk_hal_system_get_free_heap());
    } else {
        printf("Unknown command: %s\r\n", argv[1]);
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_sys, sys, sys <reset|info|heap> - system control);

/* ── Shell cmd: gpio ────────────────────────────── */

/**
 * @brief TODO: 描述cmd_gpio的功能
 *
 * @param argc TODO: 描述argc
 * @param argv TODO: 描述argv
 * @return 0成功, -1失败
 */
static int cmd_gpio(int argc, char **argv)
{
    int pin, ret;

    if (argc < 3) {
        printf("Usage: gpio <pin> <get|set 0|1>\r\n");
        printf("  e.g.: gpio 17 get\r\n");
        printf("  e.g.: gpio 17 set 1\r\n");
        return 0;
    }

    pin = atoi(argv[1]);

    if (strcmp(argv[2], "get") == 0) {
        ret = axk_hal_gpio_get_level((uint32_t)pin);
        if (ret >= 0) {
            printf("GPIO%d = %d\r\n", pin, ret);
        } else {
            printf("GPIO%d read FAILED\r\n", pin);
        }
    } else if (strcmp(argv[2], "set") == 0 && argc >= 4) {
        int level = atoi(argv[3]);
        ret = axk_hal_gpio_set_level((uint32_t)pin, (uint32_t)level);
        if (ret == 0) {
            printf("GPIO%d = %d\r\n", pin, level);
        } else {
            printf("GPIO%d set FAILED\r\n", pin);
        }
    } else {
        printf("Unknown operation: %s\r\n", argv[2]);
    }
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_gpio, gpio, gpio <pin> <get|set 0|1> - GPIO control);

/* ── Shell cmd: list ────────────────────────────── */

/**
 * @brief TODO: 描述cmd_list的功能
 *
 * @param argc TODO: 描述argc
 * @param argv TODO: 描述argv
 * @return 0成功, -1失败
 */
static int cmd_list(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("=== Registered Commands ===\r\n");
    printf("  mimi   <msg>          Send message to AI Agent\r\n");
    printf("  status                 Show system status\r\n");
    printf("  sys    <reset|info|heap> System control\r\n");
    printf("  gpio   <pin> <get|set>  GPIO control\r\n");
    printf("  list                   Show this help\r\n");
    printf("  help   <cmd>           Show command help (same as list)\r\n");
    printf("==========================\r\n");
    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_list, list, list - list all commands);
/* SDK Shell 自带 help cmd，无需重复register */

/* ── public  API ────────────────────────────────────── */

/**
 * @brief TODO: 描述axk_serial_cli_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_serial_cli_init(void)
{
    s_cli_mutex = xSemaphoreCreateMutex();
    if (!s_cli_mutex) {
        AXK_LOG_ERROR("[axk_serial_cli] Failed to create mutex\r\n");
        return -1;
    }

    AXK_LOG_INFO("[axk_serial_cli] CLI initialized, 5 diagnostic commands registered\r\n");
    printf("\r\n%s", AXK_CLI_PROMPT);
    return 0;
}

/**
 * @brief TODO: 描述axk_serial_cli_poll的功能
 *
 * @return 无返回值
 */
void axk_serial_cli_poll(void)
{
    /* Shell task (created by shell_init_with_task in main.c) handles UART RX
     * via interrupt and dispatches commands through SHELL_CMD_EXPORT handlers.
     * Do NOT read raw bytes here — that would steal input from the shell task
     * and cause all CLI commands (list/status/mimi/etc) to be unresponsive.
     *
     * The shell task is created with its own FreeRTOS task and priority,
     * so polling in application tasks is unnecessary and counterproductive.
     */
}
