/**
 * @file axk_hal_uart.c
 * @brief UART硬件抽象层实现 - 基于宏元编程平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_hal_uart.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "axk_hal_system.h"  /**< axk_hal_system_get_time_ms() */

/* ============================================================
 * 平台特定实现
 * ============================================================ */
#if AXK_PLATFORM_BL618
    #include "bflb_uart.h"
    #include "bflb_core.h"

    static struct bflb_device_s* axk_uart_devs[3] = {NULL, NULL, NULL}; /**< BL618 UART设备句柄数组 */

    /**
 * @brief 获取UART设备句柄（单例懒加载）
     *
 *
 * @param[in] port UART端口号（0/1/2）
     *
 * @return UART设备指针，端口无效返回NULL
 */
    static struct bflb_device_s* axk_uart_get_dev(uint32_t port)
    {
        if (port > 2) return NULL;
        if (axk_uart_devs[port] == NULL) {
            const char* names[] = {"uart0", "uart1", "uart2"};
            axk_uart_devs[port] = bflb_device_get_by_name(names[port]);
        }
        return axk_uart_devs[port];
    }

#elif AXK_PLATFORM_ESP32
    #include "driver/uart.h"
    static const int axk_esp32_uart_nums[] = {UART_NUM_0, UART_NUM_1, UART_NUM_2};
#endif

/* ============================================================
 * 公共实现
 * ============================================================ */

/**
 * @brief 初始化UART硬件抽象层
 *
 * @return 0 成功
 */
int axk_hal_uart_init(void)
{
    AXK_LOG_INFO("[axk_hal_uart] initUART HAL\r\n");
#if AXK_PLATFORM_BL618
    memset(axk_uart_devs, 0, sizeof(axk_uart_devs));
#endif
    return 0;
}

/**
 * @brief 配置UART端口参数（波特率、流控等）
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[in] cfg UART配置结构体指针
 * @return 0 成功，-1 参数无效
 */
int axk_hal_uart_config(uint32_t port, const axk_uart_cfg_t* cfg)
{
    if (cfg == NULL || port > 2) {
        AXK_LOG_ERROR("[axk_hal_uart] invalid param \r\n");
        return -1;
    }

    AXK_LOG_INFO("[axk_hal_uart] configport %d, 波特率%d\r\n", (int)port, (int)cfg->baudrate);

#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (uart_dev == NULL) {
        AXK_LOG_ERROR("[axk_hal_uart] get UART%ddevice FAIL\r\n", (int)port);
        return -1;
    }
    struct bflb_uart_config_s bflb_cfg = {0};
    bflb_cfg.baudrate = cfg->baudrate;
    bflb_cfg.data_bits = UART_DATA_BITS_8;
    bflb_cfg.stop_bits = UART_STOP_BITS_1;
    bflb_cfg.parity = UART_PARITY_NONE;
    bflb_cfg.flow_ctrl = cfg->flow_ctrl;
    bflb_cfg.tx_fifo_threshold = 7;
    bflb_cfg.rx_fifo_threshold = 0;
    bflb_cfg.bit_order = UART_LSB_FIRST;
    bflb_uart_init(uart_dev, &bflb_cfg);
#elif AXK_PLATFORM_ESP32
    uart_config_t uart_cfg = {0};
    uart_cfg.baud_rate = cfg->baudrate;
    uart_cfg.data_bits = UART_DATA_8_BITS;
    uart_cfg.parity = UART_PARITY_DISABLE;
    uart_cfg.stop_bits = UART_STOP_BITS_1;
    uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    uart_cfg.source_clk = UART_SCLK_DEFAULT;
    uart_param_config(axk_esp32_uart_nums[port], &uart_cfg);
    uart_driver_install(axk_esp32_uart_nums[port],
        cfg->rx_buf_size ? cfg->rx_buf_size : 256,
        cfg->tx_buf_size ? cfg->tx_buf_size : 256,
        0, NULL, 0);
#endif
    return 0;
}

/**
 * @brief UART发送单字节
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[in] ch 待发送的字节
 * @return 0 成功，-1 端口无效
 */
int axk_hal_uart_putchar(uint32_t port, uint8_t ch)
{
    if (port > 2) return -1;
#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (uart_dev) {
        bflb_uart_putchar(uart_dev, (int)ch);
    }
#elif AXK_PLATFORM_ESP32
    uart_write_bytes(axk_esp32_uart_nums[port], (const char*)&ch, 1);
#endif
    return 0;
}

/**
 * @brief UART接收单字节（带超时）
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[out] ch 接收到的字节
 * @param[in] timeout_ms 超时时间（毫秒）
 * @return 0 成功，-1 无数据或端口无效
 */
int axk_hal_uart_getchar(uint32_t port, uint8_t* ch, uint32_t timeout_ms)
{
    if (port > 2 || ch == NULL) return -1;
#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (uart_dev) {
        uint32_t start = axk_hal_system_get_time_ms();
        do {
            int c = bflb_uart_getchar(uart_dev);
            if (c >= 0) {
                *ch = (uint8_t)c;
                return 0;
            }
            taskYIELD();  /* 避免busyloop消耗CPU */
        } while ((axk_hal_system_get_time_ms() - start) < timeout_ms);
    }
    *ch = 0;
    return -1;
#elif AXK_PLATFORM_ESP32
    uint8_t c;
    int len = uart_read_bytes(axk_esp32_uart_nums[port], &c, 1, pdMS_TO_TICKS(timeout_ms));
    if (len > 0) {
        *ch = c;
        return 0;
    }
    *ch = 0;
    return -1;
#else
    *ch = 0;
    return -1;
#endif
}

/**
 * @brief UART发送多字节数据
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[in] data 待发送的数据缓冲区
 * @param[in] len 数据长度
 * @return 实际发送的字节数，0 失败
 */
int axk_hal_uart_write(uint32_t port, const uint8_t* data, uint32_t len)
{
    if (port > 2 || data == NULL || len == 0) return 0;
#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (uart_dev) {
        for (uint32_t i = 0; i < len; i++) {
            bflb_uart_putchar(uart_dev, (int)data[i]);
        }
        return (int)len;
    }
    return 0;
#elif AXK_PLATFORM_ESP32
    return uart_write_bytes(axk_esp32_uart_nums[port], (const char*)data, (size_t)len);
#else
    return 0;
#endif
}

/**
 * @brief UART接收多字节数据（带超时）
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[out] buf 接收缓冲区
 * @param[in] len 最大接收长度
 * @param[in] timeout_ms 超时时间（毫秒）
 * @return 实际接收的字节数
 */
int axk_hal_uart_read(uint32_t port, uint8_t* buf, uint32_t len, uint32_t timeout_ms)
{
    uint32_t i;
    uint32_t start_ms;

    if (port > 2 || buf == NULL || len == 0) return 0;

    start_ms = axk_hal_system_get_time_ms();

#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (!uart_dev) return 0;

    for (i = 0; i < len; i++) {
        int c;
        /* 非阻塞轮询，检查超时 */
        while ((c = bflb_uart_getchar(uart_dev)) < 0) {
            if ((axk_hal_system_get_time_ms() - start_ms) >= timeout_ms) {
                return (int)i;
            }
        }
        buf[i] = (uint8_t)c;
    }
    return (int)len;
#elif AXK_PLATFORM_ESP32
    return uart_read_bytes(axk_esp32_uart_nums[port], buf, len, pdMS_TO_TICKS(timeout_ms));
#else
    return 0;
#endif
}

/**
 * @brief 清空UART接收缓冲区
 *
 * @param[in] port UART端口号（0/1/2）
 * @return 0 成功，-1 端口无效
 */
int axk_hal_uart_flush_rx(uint32_t port)
{
    if (port > 2) return -1;
#if AXK_PLATFORM_BL618
    struct bflb_device_s* uart_dev = axk_uart_get_dev(port);
    if (uart_dev) {
        /* 读取并丢弃所有可用字节，直至FIFO为空 */
        while (bflb_uart_getchar(uart_dev) >= 0) {
            /* 什么都不做 */
        }
    }
#elif AXK_PLATFORM_ESP32
    uart_flush_input(axk_esp32_uart_nums[port]);
#endif
    return 0;
}

/**
 * @brief UART格式化输出（类似printf）
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[in] fmt 格式化字符串
 * @return 实际输出的字符数，负数失败
 */
int axk_hal_uart_printf(uint32_t port, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int plen = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (plen > 0) {
        axk_hal_uart_write(port, (uint8_t*)buf, (uint32_t)plen);
    }
    return plen;
}

/* ── DMA接收框架（硬件未接入，编译即可） ────── */

#if AXK_PLATFORM_BL618
#include "bflb_dma.h"

#define AXK_UART_DMA_BUF_SIZE  1024

#if 0  /* 硬件未接入，缓冲暂不分配 */
static uint8_t s_dma_rx_buf[3][AXK_UART_DMA_BUF_SIZE];  /**< 每路UART一个DMA缓冲 */
#endif
#if 0
static void (*s_dma_rx_cb[3])(const uint8_t *, uint32_t, void *) = {NULL};
static void *s_dma_rx_user[3] = {NULL};
#endif

/**
 * @brief 启动UART DMA接收（框架，硬件就绪后启用）
 *
 * @param[in] port UART端口号（0/1/2）
 * @param[in] cb 接收回调函数
 * @param[in] user_data 用户数据
 * @return 0 成功
 * @note TODO: 硬件接入后取消 #if 0 保护
 */
#if 0  /* 硬件未接入，编译框架用 */
/**
 * @brief 启动UART DMA接收，配置DMA通道并注册回调
 *
 * @param port UART端口号（0/1/2）
 * @param cb 接收完成回调函数
 * @param user_data 用户自定义数据
 * @return 0成功, -1失败
 */
int axk_hal_uart_dma_recv_start(uint32_t port, axk_uart_rx_cb_t cb, void *user_data)
{
    struct bflb_device_s *dma_dev;

    if (port > 2) return -1;

    s_dma_rx_cb[port] = cb;
    s_dma_rx_user[port] = user_data;

    dma_dev = bflb_device_get_by_name("dma");
    if (!dma_dev) return -1;

    /* DMA通道配置 */
    struct bflb_dma_channel_config_s dma_cfg = {
        .direction = DMA_PERIPH_TO_MEMORY,
        .src_req = DMA_REQUEST_UART0_RX + port,
        .dst_addr_inc = true,
        .src_addr_inc = false,
        .transfer_width = DMA_TRANSFER_WIDTH_8BIT,
    };

    bflb_dma_channel_init(dma_dev, &dma_cfg);
    bflb_dma_channel_start(dma_dev);

    return 0;
}

/**
 * @brief 停止UART DMA接收
 */
int axk_hal_uart_dma_recv_stop(uint32_t port)
{
    if (port > 2) return -1;
    s_dma_rx_cb[port] = NULL;
    return 0;
}
#endif /* 硬件未接入 */

#else
/**
 * @brief 启动UART DMA接收（非BL618平台桩函数）
 *
 * @param[in] port UART端口号
 * @param[in] cb 接收回调函数
 * @param[in] user_data 用户数据
 * @return -1 不支持
 */
int axk_hal_uart_dma_recv_start(uint32_t port, void *cb, void *user_data)
{
    (void)port; (void)cb; (void)user_data;
    return -1;
}
/**
 * @brief 停止UART DMA接收（非BL618平台桩函数）
 *
 * @param[in] port UART端口号
 * @return -1 不支持
 */
int axk_hal_uart_dma_recv_stop(uint32_t port)
{
    (void)port;
    return -1;
}
#endif
