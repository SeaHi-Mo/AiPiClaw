/**
 * @file axk_hal_uart.h
 * @brief UART硬件抽象层 - 宏元编程实现平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_HAL_UART_H
#define __AXK_HAL_UART_H

#include "axk_platform.h"
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================
 * UARTdevice handle 
 * ============================================================ */
typedef void* axk_uart_handle_t;

/* UART编号 */
#define AXK_UART_NUM_0          0
#define AXK_UART_NUM_1          1
#define AXK_UART_NUM_2          2

/* data位 */
#define AXK_UART_DATA_BITS_5    0
#define AXK_UART_DATA_BITS_6    1
#define AXK_UART_DATA_BITS_7    2
#define AXK_UART_DATA_BITS_8    3

/* 校验 */
#define AXK_UART_PARITY_NONE    0
#define AXK_UART_PARITY_EVEN    1
#define AXK_UART_PARITY_ODD     2

/* stop 位 */
#define AXK_UART_STOP_BITS_1    0
#define AXK_UART_STOP_BITS_1_5  1
#define AXK_UART_STOP_BITS_2    2

/* ============================================================
 * UARTconfig结构体 - 平台无关
 * ============================================================ */
typedef struct {
    uint32_t baudrate;          /**< 波特率 */
    uint32_t data_bits;         /**< data位 */
    uint32_t parity;            /**< 校验位 */
    uint32_t stop_bits;         /**< stop 位 */
    uint32_t flow_ctrl;         /**< 流控: 0=无, 1=RTS/CTS */
    uint32_t rx_buf_size;       /**< recvbuffer size */
    uint32_t tx_buf_size;       /**< sendbuffer size */
} axk_uart_cfg_t;

/* ============================================================
 * UART操作func  - 平台无关接口
 * ============================================================ */

/**
 * @brief initUARTmodule
 *
 * @return OKreturn 0
 */
int axk_hal_uart_init(void);

/**
 * @brief UART configport 
 *
 * @param[in] port port 号
 * @param[in] cfg configparam 
 * @return OKreturn 0
 */
int axk_hal_uart_config(uint32_t port, const axk_uart_cfg_t* cfg);

/**
 * @brief send单bytes
 *
 * @param[in] port port 号
 * @param[in] ch bytes
 * @return OKreturn 0
 */
int axk_hal_uart_putchar(uint32_t port, uint8_t ch);

/**
 * @brief recv单bytes
 *
 * @param[in] port port 号
 * @param[out] ch bytesptr 
 * @param[in] timeout_ms timeouttime （毫s，0=非阻塞）
 * @return OKreturn 0，timeoutreturn -1
 */
int axk_hal_uart_getchar(uint32_t port, uint8_t* ch, uint32_t timeout_ms);

/**
 * @brief senddata
 *
 * @param[in] port port 号
 * @param[in] data dataptr 
 * @param[in] len length 
 * @return 实际sendbytes数
 */
int axk_hal_uart_write(uint32_t port, const uint8_t* data, uint32_t len);

/**
 * @brief recvdata
 *
 * @param[in] port port 号
 * @param[out] buf buffer ptr 
 * @param[in] len 最大length 
 * @param[in] timeout_ms timeouttime 
 * @return 实际recvbytes数
 */
int axk_hal_uart_read(uint32_t port, uint8_t* buf, uint32_t len, uint32_t timeout_ms);

/**
 * @brief 清empty recvbuffer 
 *
 * @param[in] port port 号
 * @return OKreturn 0
 */
int axk_hal_uart_flush_rx(uint32_t port);

/**
 * @brief format 化output （类似printf）
 *
 * @param[in] port port 号
 * @param[in] fmt format chars 串
 * @param[in] ... 可变param 
 * @return printchars 数
 */
int axk_hal_uart_printf(uint32_t port, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_HAL_UART_H */
