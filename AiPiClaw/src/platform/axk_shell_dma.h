/**
 * @file axk_shell_dma.h
 * @brief Shell UART0 DMA RX 初始化接口
 */
#ifndef AXK_SHELL_DMA_H
#define AXK_SHELL_DMA_H

/**
 * @brief 将 shell UART0 RX 从字符中断切换为 DMA 接收
 *
 * 必须在 shell_init_with_task() 之后、shell_task 已运行之后调用。
 * 内部重新配置 UART RX 为 DMA 模式, 替换字符 ISR 为 RTO-only ISR,
 * DMA 数据通过 shell_rb 注入到 shell 处理循环。
 *
 * @return 0 成功, -1 失败
 */
int axk_shell_dma_init(void);

#endif
