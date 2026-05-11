/**
 * @file axk_serial_cli.h
 * @brief serial_cli module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_SERIAL_CLI_H
#define __AXK_SERIAL_CLI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化串口CLI交互模块
 * @return 0成功，负数错误码
 */
int axk_serial_cli_init(void);
/**
 * @brief 串口CLI轮询（在主循环中调用）
 * @note 处理串口输入，支持命令行交互
 */
void axk_serial_cli_poll(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_SERIAL_CLI_H */
