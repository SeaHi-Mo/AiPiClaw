/**
 * @file axk_shell_dma.c
 * @brief Shell UART0 DMA RX — 应用层重新配置，不修改 os/ 子模块
 *
 * shell_freertos.c 暴露的全局变量：
 *   extern Ring_Buffer_Type shell_rb;
 *   extern void shell_release_sem(void);
 *   extern struct bflb_device_s *uart_shell;
 *
 * 原理：shell_task 启动后 attach 了字符中断 ISR。本模块在启动后
 * 重新配置 UART0 RX 为 DMA 模式，利用 shell_rb 和 shell_release_sem
 * 向 shell 注入数据，无需修改 SDK。
 */

#include "shell.h"
#include <FreeRTOS.h>
#include "semphr.h"
#include "ring_buffer.h"
#include "bflb_uart.h"
#include "bflb_dma.h"
#include "bflb_clock.h"
#include "bflb_irq.h"

/* ── 外部符号（来自 shell_freertos.c） ───────────────── */
extern Ring_Buffer_Type shell_rb;
extern void shell_release_sem(void);
extern struct bflb_device_s *uart_shell;

/* ── DMA 配置 ────────────────────────────────────────── */
#define SHELL_DMA_BUF_SIZE    512
#define SHELL_DMA_RX_CH       "dma0_ch0"

static ATTR_NOCACHE_NOINIT_RAM_SECTION uint8_t s_dma_rx_buf[SHELL_DMA_BUF_SIZE];
static struct bflb_rx_cycle_dma s_rx_dma;
static struct bflb_dma_channel_lli_pool_s s_rx_llipool[8];
static struct bflb_device_s *s_dma_rx = NULL;
static bool s_dma_ready = false;

/* ── DMA → ring buffer 回调 ──────────────────────────── */
static void dma_rx_copy(uint8_t *data, uint32_t len)
{
    Ring_Buffer_Write(&shell_rb, data, len);
}

/* ── DMA 完成 ISR ────────────────────────────────────── */
static void shell_dma_rx_isr(void *arg)
{
    (void)arg;
    bflb_rx_cycle_dma_process(&s_rx_dma, 1);
    shell_release_sem();
}

/* ── UART RTO ISR（替换字符中断） ────────────────────── */
static void shell_uart_rto_isr(int irq, void *arg)
{
    (void)irq;
    (void)arg;
    uint32_t intstatus = bflb_uart_get_intstatus(uart_shell);

    if (intstatus & UART_INTSTS_RTO) {
        bflb_uart_int_clear(uart_shell, UART_INTCLR_RTO);
        bflb_rx_cycle_dma_process(&s_rx_dma, 0);
        shell_release_sem();
    }
}

/* ── 公开接口 ────────────────────────────────────────── */

/**
 * @brief 将 shell UART0 RX 从字符中断切换为 DMA 接收
 *
 * 调用时机：shell_init_with_task() 之后，vTaskStartScheduler() 之前。
 * shell_task 尚未运行，无竞态条件。
 *
 * @return 0 成功, -1 失败
 */
int axk_shell_dma_init(void)
{
    if (!uart_shell) {
        return -1;
    }

    struct bflb_dma_channel_config_s rx_cfg = {
        .direction = DMA_PERIPH_TO_MEMORY,
        .src_req = DMA_REQUEST_UART0_RX,
        .dst_req = DMA_REQUEST_NONE,
        .src_addr_inc = DMA_ADDR_INCREMENT_DISABLE,
        .dst_addr_inc = DMA_ADDR_INCREMENT_ENABLE,
        .src_burst_count = DMA_BURST_INCR1,
        .dst_burst_count = DMA_BURST_INCR1,
        .src_width = DMA_DATA_WIDTH_8BIT,
        .dst_width = DMA_DATA_WIDTH_8BIT,
    };

    PERIPHERAL_CLOCK_DMA0_ENABLE();

    s_dma_rx = bflb_device_get_by_name(SHELL_DMA_RX_CH);
    if (!s_dma_rx) {
        return -1;
    }

    bflb_dma_channel_init(s_dma_rx, &rx_cfg);
    bflb_dma_channel_irq_attach(s_dma_rx, shell_dma_rx_isr, NULL);

    bflb_rx_cycle_dma_init(&s_rx_dma,
                           s_dma_rx,
                           s_rx_llipool,
                           sizeof(s_rx_llipool) / sizeof(s_rx_llipool[0]),
                           uart_shell->reg_base + 0x8C,  /* UART RDR */
                           s_dma_rx_buf,
                           SHELL_DMA_BUF_SIZE,
                           dma_rx_copy);

    /* 链接 UART RX 到 DMA */
    bflb_uart_link_rxdma(uart_shell, true);

    /* 设 RTO 超时值（pause 后 flush DMA partial buffer） */
    bflb_uart_feature_control(uart_shell, UART_CMD_SET_RTO_VALUE, 0x80);

    /* 替换 shell_task 即将 attach 的字符 ISR 为 RTO-only ISR */
    bflb_irq_attach(uart_shell->irq_num, shell_uart_rto_isr, NULL);
    bflb_irq_enable(uart_shell->irq_num);

    bflb_dma_channel_start(s_dma_rx);

    s_dma_ready = true;
    return 0;
}
