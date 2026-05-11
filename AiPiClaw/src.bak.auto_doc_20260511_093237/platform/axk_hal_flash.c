/**
 * @file axk_hal_flash.c
 * @brief Flashstore硬件抽象层实现 - 基于 bflb_flash API
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 封装 bflb_flash_erase / bflb_flash_write / bflb_flash_read
 *       include 基本分区保护：denied erase fw区（0x000000 ~ fw结尾）
 *       data区建议use  LittleFS / easyflash 而非直接操作 Flash
 */

#include "axk_hal_flash.h"

#include <stdio.h>
#include <string.h>

#if AXK_PLATFORM_BL618
    #include "bflb_flash.h"
    #include "bflb_mtd.h"
#elif AXK_PLATFORM_ESP32
    #include "esp_partition.h"
#endif

/** from  flash_prog_cfg.ini 可知fw起始于 0x000000
 *  保守估计fwused 前 2MB（实际约 1.2MB）
 *  data区from  FLASH_SAFE_OFFSET start
 */
#define FLASH_SAFE_OFFSET   (2 * 1024 * 1024)  /**< 2MB, fwprotected region  */

static bool s_initialized = false;
static uint32_t s_flash_size = 0;

/* ── public  API ────────────────────────────────────── */

/**
 * @brief init Flash module
 * @return OKreturn 0
 */
int axk_hal_flash_init(void)
{
    if (s_initialized) {
        return 0;
    }

#if AXK_PLATFORM_BL618
    bflb_flash_init();
    s_flash_size = bflb_flash_get_size();
    s_initialized = true;
    AXK_LOG_INFO("[axk_hal_flash] Flashinitok, size=%luKB\r\n",
                 (unsigned long)(s_flash_size / 1024));
    return 0;
#elif AXK_PLATFORM_ESP32
    s_flash_size = 4 * 1024 * 1024;  /**< ESP32 default  4MB */
    s_initialized = true;
    return 0;
#else
    return -1;
#endif
}

/**
 * @brief erase  Flash sector （4KBaligned ）
 * @param[in] addr sector 起始addr （must 4KBaligned ）
 * @return OKreturn 0，addr in protected region return -1
 * @note auto denied erase fwprotected region  (0 ~ FLASH_SAFE_OFFSET)
 */
int axk_hal_flash_erase_sector(uint32_t addr)
{
    if (!s_initialized) {
        AXK_LOG_ERROR("[axk_hal_flash] modulenot init\r\n");
        return -1;
    }

    /* addr aligned check  */
    if (addr & (AXK_FLASH_SECTOR_SIZE - 1)) {
        AXK_LOG_ERROR("[axk_hal_flash] erase addr not 4KBaligned : 0x%08lX\r\n", (unsigned long)addr);
        return -1;
    }

    /* 分区保护：denied erase fw区 */
    if (addr < FLASH_SAFE_OFFSET) {
        AXK_LOG_ERROR("[axk_hal_flash] denied erase fwprotected region  0x%08lX (protected region  < 0x%X)\r\n",
                      (unsigned long)addr, FLASH_SAFE_OFFSET);
        return -1;
    }

#if AXK_PLATFORM_BL618
    int ret = bflb_flash_erase(addr, AXK_FLASH_SECTOR_SIZE);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_hal_flash] erase FAIL addr=0x%08lX ret=%d\r\n",
                      (unsigned long)addr, ret);
        return -1;
    }
    AXK_LOG_DEBUG("[axk_hal_flash] erase sector  0x%08lX OK\r\n", (unsigned long)addr);
    return 0;
#elif AXK_PLATFORM_ESP32
    return -1;  /**< ESP32 use 分区API，here 暂not 实现 */
#else
    return -1;
#endif
}

/**
 * @brief full chip erase （data区，not 含fw区）
 * @return OKreturn 0
 * @warning 此操作耗时较长，仅erase  FLASH_SAFE_OFFSET after data区
 */
int axk_hal_flash_erase_chip(void)
{
    uint32_t addr;
    int ret;

    if (!s_initialized) {
        AXK_LOG_ERROR("[axk_hal_flash] modulenot init\r\n");
        return -1;
    }

    AXK_LOG_WARN("[axk_hal_flash] start全data区erase  (from  0x%X  to  0x%lX)...\r\n",
                 FLASH_SAFE_OFFSET, (unsigned long)s_flash_size);

    for (addr = FLASH_SAFE_OFFSET; addr < s_flash_size; addr += AXK_FLASH_SECTOR_SIZE) {
        ret = axk_hal_flash_erase_sector(addr);
        if (ret != 0) {
            AXK_LOG_ERROR("[axk_hal_flash] full chip erase in  0x%08lX FAIL\r\n", (unsigned long)addr);
            return -1;
        }
    }

    AXK_LOG_INFO("[axk_hal_flash] 全data区erase ok\r\n");
    return 0;
}

/**
 * @brief from  Flash readdata
 * @param[in] addr 起始addr 
 * @param[out] buf output buffer 
 * @param[in] len readlength 
 * @return OKreturn 0
 */
int axk_hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (!s_initialized || !buf || len == 0) {
        return -1;
    }

    if (addr + len > s_flash_size) {
        AXK_LOG_ERROR("[axk_hal_flash] readOOB  addr=0x%08lX len=%lu\r\n",
                      (unsigned long)addr, (unsigned long)len);
        return -1;
    }

#if AXK_PLATFORM_BL618
    int ret = bflb_flash_read(addr, buf, len);
    return (ret == 0) ? 0 : -1;
#elif AXK_PLATFORM_ESP32
    return -1;
#else
    return -1;
#endif
}

/**
 * @brief  to  Flash writedata
 * @param[in] addr 起始addr 
 * @param[in] data dataptr 
 * @param[in] len writelength 
 * @return OKreturn 0
 * @note write前需先erase corresponding sector 
 */
int axk_hal_flash_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    if (!s_initialized || !data || len == 0) {
        return -1;
    }

    /* 分区保护：denied writefw区 */
    if (addr < FLASH_SAFE_OFFSET) {
        AXK_LOG_ERROR("[axk_hal_flash] denied writefwprotected region  0x%08lX\r\n", (unsigned long)addr);
        return -1;
    }

    if (addr + len > s_flash_size) {
        AXK_LOG_ERROR("[axk_hal_flash] writeOOB  addr=0x%08lX len=%lu\r\n",
                      (unsigned long)addr, (unsigned long)len);
        return -1;
    }

#if AXK_PLATFORM_BL618
    int ret = bflb_flash_write(addr, (uint8_t *)data, len);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_hal_flash] writeFAIL addr=0x%08lX ret=%d\r\n",
                      (unsigned long)addr, ret);
        return -1;
    }
    return 0;
#elif AXK_PLATFORM_ESP32
    return -1;
#else
    return -1;
#endif
}

/**
 * @brief get  Flash 总容量
 * @return Flash size（bytes）
 */
uint32_t axk_hal_flash_get_size(void)
{
    if (!s_initialized) {
        return 0;
    }
    return s_flash_size;
}
