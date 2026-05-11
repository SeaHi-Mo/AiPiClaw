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

/* @brief TODO: 描述axk_serial_cli_init的功能 @return 0成功, -1失败 */
int axk_serial_cli_init(void);
/* @brief TODO: 描述axk_serial_cli_poll的功能 @return 无返回值 */
void axk_serial_cli_poll(void);


#ifdef __cplusplus
}
#endif

#endif /* __AXK_SERIAL_CLI_H */
