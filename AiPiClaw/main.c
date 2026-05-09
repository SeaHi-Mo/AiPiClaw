/**
 * @file main.c
 * @brief MimiClaw 移植 to  BL618 main entry
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 基于 Bouffalo SDK + FreeRTOS + lwIP port
 */

#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "axk_platform.h"
#include "axk_hal_system.h"
#include "axk_hal_gpio.h"
#include "axk_hal_uart.h"
#include "axk_hal_flash.h"
#include "axk_hal_timer.h"
#include "axk_mimiclaw.h"
#include "axk_message_bus.h"
#include "axk_telegram_bot.h"

/* 各module头file */
#include "axk_ota_manager.h"
#include "axk_wifi_onboard.h"
#include "axk_wifi_manager.h"
#include "axk_ws_server.h"
#include "axk_http_proxy.h"
#include "axk_gpio_control.h"
#include "axk_llm_proxy.h"
#include "axk_telegram_bot.h"
#include "axk_feishu_bot.h"
#include "axk_mimiclaw_ext_rtc.h"
#include "axk_cron_service.h"
#include "axk_tool_files.h"

/* BL618 SDK headers */
#include "board.h"
#include "bflb_uart.h"
#include "shell.h"
#include "bflb_mtd.h"
#include "easyflash.h"
#include "lwip/tcpip.h"
#include "fhost_api.h"
#include "macsw_plat.h"
#include "wifi_mgmr_ext.h"
#include "rfparam_adapter.h"

/* forward decl: wifi_mgmr_init: explicit decl for SDK compat */
int wifi_mgmr_init(wifi_conf_t *conf);

static wifi_conf_t s_wifi_conf = {
    .country_code = "CN",
};

/**
 * @brief WiFifwstarttask
 * @param[in] param taskparam （unused）
 * @note in FreeRTOS调度器start后运行，负责initWiFi协议栈
 */
static void axk_wifi_firmware_task(void *param)
{
    (void)param;

    AXK_LOG_INFO("[axk_wifi_fw] startWiFifwinit...\r\n");

    /* createWiFitask */
    wifi_task_create();
    AXK_LOG_INFO("[axk_wifi_fw] WiFitaskcreated\r\n");

    /* initfhost */
    int ret = fhost_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_fw] fhost_init FAIL: %d\r\n", ret);
        vTaskDelete(NULL);
        return;
    }
    AXK_LOG_INFO("[axk_wifi_fw] fhost_init OK\r\n");

    /* initWiFimanager */
    ret = wifi_mgmr_init(&s_wifi_conf);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_fw] wifi_mgmr_init FAIL: %d\r\n", ret);
        vTaskDelete(NULL);
        return;
    }
    AXK_LOG_INFO("[axk_wifi_fw] wifi_mgmr_init OK\r\n");

    AXK_LOG_INFO("[axk_wifi_fw] WiFifwinitok\r\n");

    /* taskok后delete 自身 */
    vTaskDelete(NULL);
}

/**
 * @brief print项目welcome
 */
static void axk_print_welcome(void)
{
    printf("\r\n");
    printf("========================================\r\n");
    printf("  MimiClaw BL618 Port\r\n");
    printf("  %s\r\n", axk_platform_get_name());
    printf("  AI-Thinker\r\n");
    printf("  Version: v1.0.0-alpha\r\n");
    printf("========================================\r\n");
    printf("\r\n");
    axk_platform_print_capabilities();
    printf("\r\n");
}

/**
 * @brief MimiClawmain looptask
 * @param[in] param taskparam （unused）
 */
static void axk_mimiclaw_task(void *param)
{
    (void)param;
    static bool s_auto_connect_done = false;
    uint32_t startup_tick = xTaskGetTickCount();

    AXK_LOG_INFO("[axk_mimiclaw] main looptaskstart\r\n");

    while (1) {
        axk_heartbeat_tick();
        axk_serial_cli_poll();
        axk_message_bus_poll();
        axk_wifi_manager_poll();
        axk_gpio_control_poll();
        axk_agent_loop_run();
        /* delay auto-connect， etc待WiFifwinitok */
        if (!s_auto_connect_done &&
            (xTaskGetTickCount() - startup_tick) > pdMS_TO_TICKS(3000)) {
            s_auto_connect_done = true;
            if (axk_wifi_get_state() == AXK_WIFI_STATE_DISCONNECTED) {
                AXK_LOG_INFO("[axk_mimiclaw] attempting saved WiFi connect...\r\n");
                axk_wifi_auto_connect();
            }
        }

        /* outbound dispatch */
        {
            mimi_msg_t out_msg;
            if (axk_message_bus_pop_outbound(&out_msg, 0) == 0) {
                if (strcmp(out_msg.channel, MIMI_CHAN_CLI) == 0) {
                    printf("\r\n[AI] %s\r\n\r\nmimi> ", out_msg.content ? out_msg.content : "(empty)");
                } else if (strcmp(out_msg.channel, MIMI_CHAN_TELEGRAM) == 0) {
                    axk_telegram_send_message(out_msg.chat_id, out_msg.content ? out_msg.content : "");
                } else if (strcmp(out_msg.channel, MIMI_CHAN_FEISHU) == 0) {
                    axk_feishu_send_message(out_msg.chat_id, out_msg.content ? out_msg.content : "");
                } else if (strcmp(out_msg.channel, MIMI_CHAN_WEBSOCKET) == 0) {
                    axk_ws_server_send(out_msg.content ? out_msg.content : "");
                } else {
                    AXK_LOG_WARN("[axk_mimiclaw] unknown channel: %s\r\n", out_msg.channel);
                }
                if (out_msg.content) {
                    free(out_msg.content);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/**
 * @brief initMimiClaw移植项目allmodule
 * @return OKreturn 0，FAILreturn 非零
 */
static int axk_mimiclaw_modules_init(void)
{
    int ret = 0;

    AXK_LOG_INFO("[axk_mimiclaw] startinitallmodule...\r\n");

    ret = axk_hal_system_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_mimiclaw] system HALinitFAIL: %d\r\n", ret);
        return ret;
    }
    AXK_LOG_INFO("[axk_mimiclaw] system HALinitOK\r\n");

    ret = axk_hal_gpio_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_mimiclaw] GPIO HALinitFAIL: %d\r\n", ret);
        return ret;
    }
    AXK_LOG_INFO("[axk_mimiclaw] GPIO HALinitOK\r\n");

    ret = axk_hal_uart_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_mimiclaw] UART HALinitFAIL: %d\r\n", ret);
        return ret;
    }
    AXK_LOG_INFO("[axk_mimiclaw] UART HALinitOK\r\n");

    ret = axk_hal_flash_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_mimiclaw] Flash HALinitFAIL: %d\r\n", ret);
        return ret;
    }
    AXK_LOG_INFO("[axk_mimiclaw] Flash HALinitOK\r\n");

    ret = axk_hal_timer_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_mimiclaw] timerHALinitFAIL: %d\r\n", ret);
        return ret;
    }
    AXK_LOG_INFO("[axk_mimiclaw] timerHALinitOK\r\n");

    ret = axk_heartbeat_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] heartbeatmoduleinitOK\r\n");

    ret = axk_tool_files_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] filesysteminitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] filesysteminitOK\r\n");

    ret = axk_memory_store_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] memory mgrmoduleinitOK\r\n");

    ret = axk_session_mgr_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] session mgrmoduleinitOK\r\n");

    ret = axk_message_bus_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] message busmoduleinitOK\r\n");

    ret = axk_wifi_manager_init();
    if (ret != 0) {
        AXK_LOG_WARN("[axk_mimiclaw] WiFimgrmoduleinitWARN: %d\r\n", ret);
    } else {
        AXK_LOG_INFO("[axk_mimiclaw] WiFimgrmoduleinitOK\r\n");
    }

    ret = axk_serial_cli_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] serial CLImoduleinitOK\r\n");

    ret = axk_tool_registry_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] tool registrymoduleinitOK\r\n");

    ret = axk_skill_loader_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] skill loadermoduleinitOK\r\n");

    ret = axk_ext_rtc_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] external RTCinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] external RTCmoduleinitOK\r\n");

#if 1  /* FIXME: skip modules that may crash before agent starts */
    AXK_LOG_INFO("[axk_mimiclaw] agent_loop_init...\r\n");
    ret = axk_agent_loop_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] agentmain loopmoduleinitOK\r\n");

    AXK_LOG_INFO("[axk_mimiclaw] agent_loop_start...\r\n");
    ret = axk_agent_loop_start();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] agentmain looptaskstartOK\r\n");
    
    /* skip remaining modules for now */
    AXK_LOG_INFO("[axk_mimiclaw] allmoduleinitok (minimal)\r\n");
    return 0;
#endif

    ret = axk_llm_proxy_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] LLM agentinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] LLM agentmoduleinitOK\r\n");

    ret = axk_cron_service_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] crontaskserviceinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] crontaskserviceinitOK\r\n");

    ret = axk_ota_manager_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] OTA mgrinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] OTA mgrmoduleinitOK\r\n");

    ret = axk_wifi_onboard_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] WiFi provisioningmoduleinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] WiFi provisioningmoduleinitOK\r\n");

    ret = axk_http_proxy_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] HTTP proxyinitWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] HTTP proxymoduleinitOK\r\n");

    ret = axk_ws_server_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] WebSocket serverinitWARN: %d\r\n", ret);
    else {
        AXK_LOG_INFO("[axk_mimiclaw] WebSocket servermoduleinitOK\r\n");
        axk_ws_server_start(MIMI_WS_PORT);
    }

    ret = axk_telegram_bot_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] Telegram BotinitWARN: %d\r\n", ret);
    else {
        AXK_LOG_INFO("[axk_mimiclaw] Telegram BotmoduleinitOK\r\n");
        axk_telegram_bot_start();
    }

    ret = axk_feishu_bot_init();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] Feishu BotinitWARN: %d\r\n", ret);
    else {
        AXK_LOG_INFO("[axk_mimiclaw] Feishu BotmoduleinitOK\r\n");
        axk_feishu_bot_start();
    }

    ret = axk_agent_loop_init();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] agentmain loopmoduleinitOK\r\n");

    ret = axk_agent_loop_start();
    if (ret != 0) return ret;
    AXK_LOG_INFO("[axk_mimiclaw] agentmain looptaskstartOK\r\n");

    ret = axk_cron_service_start();
    if (ret != 0) AXK_LOG_WARN("[axk_mimiclaw] crontaskservicestartWARN: %d\r\n", ret);
    else AXK_LOG_INFO("[axk_mimiclaw] crontaskservicestartOK\r\n");

    AXK_LOG_INFO("[axk_mimiclaw] allmoduleinitok\r\n");
    return 0;
}

/**
 * @brief MimiClaw BL618移植工程主func 
 * @return program does not return
 */
int main(void)
{
    struct bflb_device_s *uart0;

    /* init开发板硬件 */
    board_init();

    /* initPHY RFparam  */
    if (rfparam_init(0, NULL, 0) != 0) {
        printf("[main] PHY RF init FAIL\r\n");
    } else {
        printf("[main] PHY RF init OK\r\n");
    }

    /* initMTD subsystem（供easyflashuse ） */
    bflb_mtd_init();

    /* initeasyflash KV store */
    if (easyflash_init() != EF_NO_ERR) {
        printf("[main] easyflash_init FAIL\r\n");
    } else {
        printf("[main] easyflash_init OK\r\n");
    }

    /* initlwIP TCP/IP stack */
    tcpip_init(NULL, NULL);
    printf("[main] tcpip_init ok\r\n");

    /* get UART0device  and initshell
     * note: : board_init → console_init config UART0 (GPIO21/22, rx_fifo_threshold=7)
     * do not call bflb_uart_init，否then 会破坏 console_init set  */
    uart0 = bflb_device_get_by_name("uart0");
    shell_init_with_task(uart0);

    /* printwelcome */
    axk_print_welcome();

    /* initMimiClaw各module */
    int ret = axk_mimiclaw_modules_init();
    if (ret != 0) {
        AXK_LOG_ERROR("[main] MimiClawmoduleinitFAIL，system will reboot...\r\n");
        AXK_DELAY_MS(2000);
        axk_hal_system_reset();
        return -1;
    }

    AXK_LOG_INFO("[main] MimiClaw BL618portstartOK，createmain looptask...\r\n");

    /* createMimiClawmain looptask */
    BaseType_t task_ret = xTaskCreate(
        axk_mimiclaw_task,
        "mimi_main",
        4096,
        NULL,
        5,
        NULL
    );

    if (task_ret != pdPASS) {
        AXK_LOG_ERROR("[main] createmain looptaskFAIL\r\n");
        axk_hal_system_reset();
        return -1;
    }

    /* createWiFifwstarttask（高priority ，ensure WiFi早期init） */
    task_ret = xTaskCreate(
        axk_wifi_firmware_task,
        "wifi_fw",
        8192,
        NULL,
        10,
        NULL
    );

    if (task_ret != pdPASS) {
        AXK_LOG_ERROR("[main] createWiFifwtaskFAIL\r\n");
        axk_hal_system_reset();
        return -1;
    }

    /* startFreeRTOS调度器 */
    vTaskStartScheduler();

    /* here not should  to 达 */
    while (1) {
    }
}

/**
 * @brief Shellcmd：llm_key <api_key> —— set  LLM API key 
 * @note key 会persist to  Flash，下次startauto load
 */
static void cmd_llm_key(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: : llm_key <api_key>\r\n");
        printf("示eg.: llm_key sk-ant-api03-xxxxx\r\n");
        printf("\r\n");
        printf("note: : key length 建议not 超过64chars \r\n");
        return;
    }

    int ret = axk_llm_set_api_key(argv[1]);
    if (ret == 0) {
        printf("[llm_key] APIkey save  to Flash (length : %d)\r\n", (int)strlen(argv[1]));
    } else {
        printf("[llm_key] save FAIL: %d\r\n", ret);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_llm_key, llm_key, set LLM APIkey );

/**
 * @brief Shellcmd：llm_model <model> —— set  LLM 模型name 
 * @note 模型name 会persist to  Flash
 */
static void cmd_llm_model(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: : llm_model <model>\r\n");
        printf("current default : %s\r\n", MIMI_LLM_DEFAULT_MODEL);
        printf("示eg.:\r\n");
        printf("  llm_model claude-opus-4-5\r\n");
        printf("  llm_model claude-sonnet-4-20250514\r\n");
        printf("  llm_model gpt-4o\r\n");
        return;
    }

    int ret = axk_llm_set_model(argv[1]);
    if (ret == 0) {
        printf("[llm_model] 模型set 为: %s\r\n", argv[1]);
    } else {
        printf("[llm_model] set FAIL: %d\r\n", ret);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_llm_model, llm_model, set LLM模型name );

/**
 * @brief Shellcmd：llm_provider <provider> —— set  LLM provide 商
 * @note provide 商name 会persist to  Flash
 */
static void cmd_llm_provider(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: : llm_provider <provider>\r\n");
        printf("current default : %s\r\n", MIMI_LLM_PROVIDER_DEFAULT);
        printf("可选value :\r\n");
        printf("  anthropic  - Anthropic Claude API\r\n");
        printf("  openai     - OpenAI API\r\n");
        printf("  deepseek   - DeepSeek API\r\n");
        printf("  minimax    - MiniMax API\r\n");
        return;
    }

    int ret = axk_llm_set_provider(argv[1]);
    if (ret == 0) {
        printf("[llm_provider] provide 商set 为: %s\r\n", argv[1]);
    } else {
        printf("[llm_provider] set FAIL: %d (有效value : anthropic/openai/deepseek/minimax)\r\n", ret);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_llm_provider, llm_provider, set LLMprovide 商);
