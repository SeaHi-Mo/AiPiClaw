/**
 * @file axk_shell_dma.c
 * @brief Shell UART0 DMA RX — 应用层重新配置，不修改 os/ 子模块
 *
 * 两阶段初始化:
 *   Phase-1 (main, before scheduler): DMA通道配置 + UART DMA链接
 *   Phase-2 (mimiclaw task, after scheduler): ISR 替换（shell_task 已 attach 字符 ISR）
 */

#include "shell.h"
#include <FreeRTOS.h>
#include "task.h"
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

/* ── UART RTO ISR（替换 shell_task 的字符 ISR） ──────── */
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
 * @brief Phase-1: DMA 通道配置 + UART DMA 链接
 *
 * 必须在 vTaskStartScheduler() 之前调用。
 * shell_task 尚未运行，UART ISR 未 attach，无竞态。
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

    /* 链接 UART RX 到 DMA（scheduler 未启动，UART 静默） */
    bflb_uart_link_rxdma(uart_shell, true);

    /* 设 RTO 超时值 */
    bflb_uart_feature_control(uart_shell, UART_CMD_SET_RTO_VALUE, 0x80);

    /* 先不 attach UART ISR — shell_task 会 attach 自己的，
     * Phase-2 axk_shell_dma_isr_attach() 再替换 */
    bflb_dma_channel_start(s_dma_rx);

    s_dma_ready = true;
    return 0;
}

/**
 * @brief Phase-2: 替换 UART ISR 为 RTO-only ISR
 *
 * shell_task 在启动时 attach 了字符 ISR (uart_shell_isr)。
 * 本函数重新 attach 为 RTO-only ISR，DMA 完成 ISR 不受影响。
 *
 * 调用时机：scheduler 启动后，shell_task 完成 ISR 初始化后。
 */
void axk_shell_dma_isr_attach(void)
{
    if (!s_dma_ready || !uart_shell) {
        return;
    }

    /* 重新 attach UART ISR 为 RTO-only */
    bflb_irq_attach(uart_shell->irq_num, shell_uart_rto_isr, NULL);
}
