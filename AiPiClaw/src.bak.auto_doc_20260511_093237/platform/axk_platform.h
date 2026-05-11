/**
 * @file axk_platform.h
 * @brief 平台抽象层入口 - use 宏元编程实现平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 本file为MimiClaw移植核心，via 宏元编程实现ESP32&BL618完全解耦
 *
 * @par 设计理念
 * 采用宏元编程（Macro Metaprogramming）技术，in 编译during ok平台选择&代码生成，
 * 运行时零开销为零，实现真正"编译期多态"。
 */

#ifndef __AXK_PLATFORM_H
#define __AXK_PLATFORM_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ESP-IDF 兼容层 - 为上游代码provide  int  etctype  */

/* ============================================================
 * 平台auto detect 宏
 * ============================================================ */

/**
 * @brief auto detect current 编译平台
 * 
 * priority ：
 * 1. 显式定义 CONFIG_AXK_PLATFORM_XXX
 * 2. BL618 SDK 自带 BL616/BL618 宏
 * 3. ESP-IDF 自带 ESP_PLATFORM 宏
 */
#if defined(CONFIG_AXK_PLATFORM_BL618) || defined(BL616) || defined(BL618) || defined(CONFIG_CHIP_BL616) || defined(CONFIG_CHIP_BL618)
    #define AXK_PLATFORM_BL618      1
    #define AXK_PLATFORM_NAME       "BL618"
    #define AXK_PLATFORM_STRING     "Bouffalo BL618 (RISC-V)"
#elif defined(CONFIG_AXK_PLATFORM_ESP32) || defined(ESP_PLATFORM) || defined(ESP32) || defined(CONFIG_IDF_TARGET_ESP32)
    #define AXK_PLATFORM_ESP32      1
    #define AXK_PLATFORM_NAME       "ESP32"
    #define AXK_PLATFORM_STRING     "Espressif ESP32 (Xtensa)"
#else
    /* default 假设为BL618平台（移植目标） */
    #define AXK_PLATFORM_BL618      1
    #define AXK_PLATFORM_NAME       "BL618"
    #define AXK_PLATFORM_STRING     "Bouffalo BL618 (RISC-V)"
    #warning "No platform detected, defaulting to BL618. Define CONFIG_AXK_PLATFORM_BL618 or CONFIG_AXK_PLATFORM_ESP32 explicitly."
#endif

/* ============================================================
 * 平台能力声明宏 - X-Macros 技术
 * ============================================================ */

/**
 * @brief 平台能力表
 * 
 * use X-Macros技术定义各平台support 功能特性。
 * via 扩展此list 可auto 生成平台能力查询代码。
 */
#define AXK_PLATFORM_CAPABILITY_TABLE \
    AXK_CAP_ENTRY(GPIO,          "General Purpose IO",        1, 1) \
    AXK_CAP_ENTRY(UART,          "Serial Communication",      1, 1) \
    AXK_CAP_ENTRY(WIFI,          "WiFi Wireless",             1, 1) \
    AXK_CAP_ENTRY(BLE,           "Bluetooth Low Energy",      0, 1) \
    AXK_CAP_ENTRY(SPIFFS,        "SPI Flash File System",     1, 0) \
    AXK_CAP_ENTRY(LITTLEFS,      "Little File System",        0, 1) \
    AXK_CAP_ENTRY(OTA,           "Over-The-Air Update",       1, 1) \
    AXK_CAP_ENTRY(PWM,           "Pulse Width Modulation",    1, 1) \
    AXK_CAP_ENTRY(I2C,           "Inter-Integrated Circuit",  1, 1) \
    AXK_CAP_ENTRY(SPI,           "Serial Peripheral Interface",1,1) \
    AXK_CAP_ENTRY(ADC,           "Analog to Digital",         1, 1) \
    AXK_CAP_ENTRY(DMA,           "Direct Memory Access",      1, 1) \
    AXK_CAP_ENTRY(PSRAM,         "Pseudo SRAM",               1, 1) \
    AXK_CAP_ENTRY(MULTICORE,     "Multi-core Support",        1, 0) \
    AXK_CAP_ENTRY(FREERTOS,      "FreeRTOS Support",          1, 1)

/**
 * @brief 生成平台能力枚举定义
 * 
 * via 宏元编程will 能力表convert 为枚举常量。
 */
enum axk_platform_capability {
    #define AXK_CAP_ENTRY(name, desc, esp32, bl618) AXK_CAP_##name,
    AXK_PLATFORM_CAPABILITY_TABLE
    #undef AXK_CAP_ENTRY
    AXK_CAP_COUNT
};

/**
 * @brief 平台能力detect 宏
 * 
 * in 编译during 确定特定功能is否可用。
 */
#define AXK_PLATFORM_HAS(cap_name) \
    ( (AXK_PLATFORM_ESP32 && (AXK_CAP_##cap_name##_ESP32)) || \
      (AXK_PLATFORM_BL618 && (AXK_CAP_##cap_name##_BL618)) )

/* 生成各能力in 各平台support status  */
#define AXK_CAP_GPIO_ESP32          1
#define AXK_CAP_UART_ESP32          1
#define AXK_CAP_WIFI_ESP32          1
#define AXK_CAP_BLE_ESP32           0
#define AXK_CAP_SPIFFS_ESP32        1
#define AXK_CAP_LITTLEFS_ESP32      0
#define AXK_CAP_OTA_ESP32           1
#define AXK_CAP_PWM_ESP32           1
#define AXK_CAP_I2C_ESP32           1
#define AXK_CAP_SPI_ESP32           1
#define AXK_CAP_ADC_ESP32           1
#define AXK_CAP_DMA_ESP32           1
#define AXK_CAP_PSRAM_ESP32         1
#define AXK_CAP_MULTICORE_ESP32     1
#define AXK_CAP_FREERTOS_ESP32      1

#define AXK_CAP_GPIO_BL618          1
#define AXK_CAP_UART_BL618          1
#define AXK_CAP_WIFI_BL618          1
#define AXK_CAP_BLE_BL618           1
#define AXK_CAP_SPIFFS_BL618        0
#define AXK_CAP_LITTLEFS_BL618      1
#define AXK_CAP_OTA_BL618           1
#define AXK_CAP_PWM_BL618           1
#define AXK_CAP_I2C_BL618           1
#define AXK_CAP_SPI_BL618           1
#define AXK_CAP_ADC_BL618           1
#define AXK_CAP_DMA_BL618           1
#define AXK_CAP_PSRAM_BL618         1
#define AXK_CAP_MULTICORE_BL618     0
#define AXK_CAP_FREERTOS_BL618      1

/* ============================================================
 * 平台特定头fileinclude 
 * ============================================================ */

#if AXK_PLATFORM_BL618
    #include "axk_platform_bl618.h"
#elif AXK_PLATFORM_ESP32
    #include "axk_platform_esp32.h"
#endif

/* ============================================================
 * 通用平台接口宏 - 零开销抽象
 * ============================================================ */

/**
 * @brief 平台init宏
 * auto call 平台特定systeminitfunc 。
 */
#define AXK_PLATFORM_INIT() \
    AXK_PLATFORM_IMPL_INIT()

/**
 * @brief 平台延时宏
 * 封装平台特定延时func 。
 */
#define AXK_DELAY_MS(ms)            AXK_PLATFORM_IMPL_DELAY_MS(ms)
#define AXK_DELAY_US(us)            AXK_PLATFORM_IMPL_DELAY_US(us)

/**
 * @brief 平台log output 宏
 * 统一封装printf风格log output 。
 */
#define AXK_LOG(fmt, ...)           AXK_PLATFORM_IMPL_LOG(fmt, ##__VA_ARGS__)
#define AXK_LOG_ERROR(fmt, ...)     AXK_PLATFORM_IMPL_LOG_ERROR(fmt, ##__VA_ARGS__)
#define AXK_LOG_WARN(fmt, ...)      AXK_PLATFORM_IMPL_LOG_WARN(fmt, ##__VA_ARGS__)
#define AXK_LOG_INFO(fmt, ...)      AXK_PLATFORM_IMPL_LOG_INFO(fmt, ##__VA_ARGS__)
#define AXK_LOG_DEBUG(fmt, ...)     AXK_PLATFORM_IMPL_LOG_DEBUG(fmt, ##__VA_ARGS__)

/**
 * @brief 关断宏
 * 封裉平台特定断mgr。
 */
#define AXK_ENTER_CRITICAL()        AXK_PLATFORM_IMPL_ENTER_CRITICAL()
#define AXK_EXIT_CRITICAL()         AXK_PLATFORM_IMPL_EXIT_CRITICAL()

/**
 * @brief 内存屏障宏
 * ensure 编译器not 重排内存访问。
 */
#define AXK_MEMORY_BARRIER()        AXK_PLATFORM_IMPL_MEMORY_BARRIER()

/* ============================================================
 * 平台INFO查询func 
 * ============================================================ */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief get 平台name chars 串
 * @return 平台name chars 串
 */
static inline const char* axk_platform_get_name(void)
{
    return AXK_PLATFORM_STRING;
}

/**
 * @brief check 平台能力
 * @param[in] cap 能力枚举value 
 * @return support return true，not support return false
 */
static inline bool axk_platform_has_capability(enum axk_platform_capability cap)
{
    switch (cap) {
        #define AXK_CAP_ENTRY(name, desc, esp32, bl618) \
            case AXK_CAP_##name: return AXK_PLATFORM_BL618 ? bl618 : esp32;
        AXK_PLATFORM_CAPABILITY_TABLE
        #undef AXK_CAP_ENTRY
        default:
            return false;
    }
}

/**
 * @brief print平台能力list 
 * for debug  & 诊断。
 */
static inline void axk_platform_print_capabilities(void)
{
    AXK_LOG_INFO("Platform: %s\r\n", AXK_PLATFORM_STRING);
    AXK_LOG_INFO("Capabilities:\r\n");
    #define AXK_CAP_ENTRY(name, desc, esp32, bl618) \
        AXK_LOG_INFO("  %-12s %s\r\n", #name, \
            axk_platform_has_capability(AXK_CAP_##name) ? "[OK]" : "[--]");
    AXK_PLATFORM_CAPABILITY_TABLE
    #undef AXK_CAP_ENTRY
}

#ifdef __cplusplus
}
#endif

#endif /* __AXK_PLATFORM_H */
