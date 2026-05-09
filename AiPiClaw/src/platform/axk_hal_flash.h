/**
 * @file axk_hal_flash.h
 * @brief Flashstore硬件抽象层
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_HAL_FLASH_H
#define __AXK_HAL_FLASH_H
#include "axk_platform.h"
#ifdef __cplusplus
extern "C" {
#endif

#define AXK_FLASH_SECTOR_SIZE   4096
#define AXK_FLASH_PAGE_SIZE     256

int axk_hal_flash_init(void);
int axk_hal_flash_erase_sector(uint32_t addr);
int axk_hal_flash_erase_chip(void);
int axk_hal_flash_read(uint32_t addr, uint8_t* buf, uint32_t len);
int axk_hal_flash_write(uint32_t addr, const uint8_t* data, uint32_t len);
uint32_t axk_hal_flash_get_size(void);

#ifdef __cplusplus
}
#endif
#endif /* __AXK_HAL_FLASH_H */
