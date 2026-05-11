/**
 * @file axk_hal_flash.h
 * @brief Flash存储硬件抽象层
 * @details BL618平台通过MTD设备接口访问内部Flash，支持扇区擦除和页写入。
 *          Flash布局由分区表（partition table）定义，EasyFlash使用"media"分区。
 *          扇区大小4096字节，页大小256字节，4K擦除粒度。
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_FLASH_H
#define __AXK_HAL_FLASH_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Flash扇区擦除粒度（BL618标准：4KB）
 *
 */
#define AXK_FLASH_SECTOR_SIZE   4096
/**
 * @brief Flash页写入粒度（BL618标准：256B）
 *
 */
#define AXK_FLASH_PAGE_SIZE     256

/**
 * @brief 初始化Flash子系统
 *
 * @return 0成功，负数错误码
 */
int axk_hal_flash_init(void);
/**
 * @brief 擦除指定扇区
 *
 * @param addr 扇区地址（需4K对齐）
 * @return 0成功，负数错误码
 */
int axk_hal_flash_erase_sector(uint32_t addr);
/**
 * @brief 全片擦除Flash
 *
 * @return 0成功，负数错误码
 */
int axk_hal_flash_erase_chip(void);
/**
 * @brief 从Flash读取数据
 *
 * @param addr 起始地址
 * @param buf 输出缓冲区
 * @param len 读取长度
 * @return 0成功，负数错误码
 */
int axk_hal_flash_read(uint32_t addr, uint8_t* buf, uint32_t len);
/**
 * @brief 向Flash写入数据
 *
 * @param addr 起始地址
 * @param data 数据源
 * @param len 写入长度
 * @return 0成功，负数错误码
 */
int axk_hal_flash_write(uint32_t addr, const uint8_t* data, uint32_t len);
/**
 * @brief 获取Flash总容量
 *
 * @return Flash大小，单位字节
 */
uint32_t axk_hal_flash_get_size(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_FLASH_H */
