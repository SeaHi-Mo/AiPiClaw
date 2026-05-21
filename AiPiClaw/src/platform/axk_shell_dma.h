/**
 * @file axk_shell_dma.h
 * @brief Shell UART0 DMA RX 初始化接口（两阶段）
 */
#ifndef AXK_SHELL_DMA_H
#define AXK_SHELL_DMA_H

/**
 * @brief Phase-1: DMA 通道配置 + UART DMA 链接
 *
 * 必须在 shell_init_with_task() 之后、vTaskStartScheduler() 之前调用。
 * UART 此时空闲，无 ISR 冲突。
 *
 * @return 0 成功, -1 失败
 */
int axk_shell_dma_init(void);

/**
 * @brief Phase-2: 替换 UART ISR 为 RTO-only ISR
 *
 * shell_task 在调度器启动后 attach 了字符 ISR。
 * 本函数重新 attach 为 RTO-only ISR。
 * 调用时机：scheduler 已运行，shell_task 已完成 ISR 初始化后。
 */
void axk_shell_dma_isr_attach(void);

#endif
