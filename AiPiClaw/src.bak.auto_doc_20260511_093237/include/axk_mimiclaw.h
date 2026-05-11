/**
 * @file axk_mimiclaw.h
 * @brief MimiClaw BL618移植项目主头file
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 定义allmoduleinit & 运行接口
 */

#ifndef __AXK_MIMICLAW_H
#define __AXK_MIMICLAW_H

#include <stdint.h>
#include "axk_platform.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===================== heartbeatmodule ===================== */
/**
 * @brief initheartbeatmodule
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_heartbeat_init(void);

/**
 * @brief heartbeatmodulecron执行（main loopcall ）
 */
void axk_heartbeat_tick(void);

/* ===================== memory mgrmodule ===================== */
/**
 * @brief init内存storemodule
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_memory_store_init(void);

/* ===================== session mgrmodule ===================== */
/**
 * @brief initsession mgr器
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_session_mgr_init(void);

/* ===================== message busmodule ===================== */
/**
 * @brief initmessage bus
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_message_bus_init(void);

/**
 * @brief poll message bus事件
 */
void axk_message_bus_poll(void);

/* ===================== WiFimgrmodule ===================== */
/**
 * @brief initWiFimanager
 * @return OKreturn 0，FAILreturn 非零
 * @note need 先okWiFi硬件init
 */
int axk_wifi_manager_init(void);

/**
 * @brief poll WiFi事件（connectstatus 、断线reconnect  etc）
 */
void axk_wifi_manager_poll(void);

/* ===================== serial CLImodule ===================== */
/**
 * @brief initserial CLI界面
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_serial_cli_init(void);

/**
 * @brief poll serial CLIinput 
 */
void axk_serial_cli_poll(void);

/* ===================== tool registrymodule ===================== */
/**
 * @brief inittool registry
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_tool_registry_init(void);

/* ===================== skill loadermodule ===================== */
/**
 * @brief initskill loader
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_skill_loader_init(void);

/* ===================== agentmain loopmodule ===================== */
/**
 * @brief initagentmain loop
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_agent_loop_init(void);

/**
 * @brief startagentmain looptask
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_agent_loop_start(void);

/**
 * @brief 执行agentmain loop一次迭代
 */
void axk_agent_loop_run(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MIMICLAW_H */
