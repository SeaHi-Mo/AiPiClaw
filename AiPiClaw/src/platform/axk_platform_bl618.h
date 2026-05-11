/**
 * @file axk_platform_bl618.h
 * @brief BL618平台特定实现 - 宏元编程封装层
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note will Bouffalo SDK API封装为平台无关接口，实现宏元解耦
 */

#ifndef __AXK_PLATFORM_BL618_H
#define __AXK_PLATFORM_BL618_H

/* ============================================================
 * BL618 SDK headersinclude 
 * ============================================================ */
#include "bflb_mtimer.h"
#include "bflb_uart.h"
#include "bflb_gpio.h"
#include "bflb_flash.h"
#include "board.h"

/* ============================================================
 * 平台init实现
 * ============================================================ */
#define AXK_PLATFORM_IMPL_INIT() \
    do { \
        board_init(); \
        board_uartx_gpio_init(); \
    } while(0)

/* ============================================================
 * 延时实现
 * ============================================================ */
#define AXK_PLATFORM_IMPL_DELAY_MS(ms)      bflb_mtimer_delay_ms(ms)
#define AXK_PLATFORM_IMPL_DELAY_US(us)      bflb_mtimer_delay_us(us)

/* ============================================================
 * log 实现 - via UART0output 
 * ============================================================ */
#define AXK_PLATFORM_IMPL_LOG(fmt, ...) \
    printf(fmt, ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_ERROR(fmt, ...) \
    printf("[E] " fmt "\r\n", ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_WARN(fmt, ...) \
    printf("[W] " fmt "\r\n", ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_INFO(fmt, ...) \
    printf("[I] " fmt "\r\n", ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_DEBUG(fmt, ...) \
    printf("[D] " fmt "\r\n", ##__VA_ARGS__)

/* ============================================================
 * 断mgr实现
 * ============================================================ */
#define AXK_PLATFORM_IMPL_ENTER_CRITICAL() \
    __disable_irq()

#define AXK_PLATFORM_IMPL_EXIT_CRITICAL() \
    __enable_irq()

/* ============================================================
 * 内存屏障实现 - RISC-V指令
 * ============================================================ */
#define AXK_PLATFORM_IMPL_MEMORY_BARRIER() \
    __asm__ volatile ("fence" ::: "memory")

/* ============================================================
 * GPIO 宏抽象 - will BL618 GPIO API映射为平台无关接口
 * ============================================================ */

/* GPIO方 to 枚举 - in axk_hal_gpio.h定义，here not 再重复定义避免冲突 */
/* 需底层映射参考，请in HAL层实现use  */

/* GPIOstatus  */
#define AXK_GPIO_LEVEL_LOW      0
#define AXK_GPIO_LEVEL_HIGH     1

/* GPIO宏操作 - 零开销抽象 */
#define AXK_GPIO_INIT(pin, mode) \
    do { \
        struct bflb_gpio_init_s gpio_cfg = {0}; \
        gpio_cfg.pin = pin; \
        gpio_cfg.mode = mode; \
        bflb_gpio_init(gpio_cfg); \
    } while(0)

#define AXK_GPIO_SET_LEVEL(pin, level) \
    bflb_gpio_set(pin, level)

#define AXK_GPIO_GET_LEVEL(pin) \
    bflb_gpio_read(pin)

#define AXK_GPIO_TOGGLE(pin) \
    bflb_gpio_toggle(pin)

/* ============================================================
 * UART 宏抽象 - 封装BL618 UARTconfig
 * ============================================================ */
#define AXK_UART_DEFAULT_BAUDRATE   2000000
#define AXK_UART_DEFAULT_DEVICE     "uart0"

/* UARTconfig宏 - 一步okconfig */
#define AXK_UART_CONFIG(uart_dev, baud) \
    do { \
        struct bflb_uart_config_s cfg = {0}; \
        cfg.baudrate = baud; \
        cfg.data_bits = UART_DATA_BITS_8; \
        cfg.stop_bits = UART_STOP_BITS_1; \
        cfg.parity = UART_PARITY_NONE; \
        cfg.flow_ctrl = 0; \
        cfg.tx_fifo_threshold = 7; \
        cfg.rx_fifo_threshold = 7; \
        cfg.bit_order = UART_LSB_FIRST; \
        bflb_uart_init(uart_dev, &cfg); \
    } while(0)

/* UARTdatasend宏 */
#define AXK_UART_PUTCHAR(uart_dev, ch) \
    bflb_uart_putchar(uart_dev, ch)

#define AXK_UART_GETCHAR(uart_dev) \
    bflb_uart_getchar(uart_dev)

/* ============================================================
 * Flash/store 宏抽象
 * ============================================================ */
#define AXK_FLASH_ERASE_CHIP()          bflb_flash_erase_chip()
#define AXK_FLASH_ERASE_SECTOR(addr)    bflb_flash_erase(addr, 4096)
#define AXK_FLASH_READ(addr, buf, len)  bflb_flash_read(addr, buf, len)
#define AXK_FLASH_WRITE(addr, buf, len) bflb_flash_write(addr, buf, len)

/* ============================================================
 * systemINFO 宏抽象
 * ============================================================ */
#define AXK_GET_CHIP_NAME()             "BL616/BL618"
#define AXK_GET_SDK_VERSION()           "bouffalo_sdk_v2.0"
#define AXK_GET_CPU_FREQ_MHZ()          320
#define AXK_GET_HEAP_SIZE()             (512 * 1024)  /* 512KB SRAM */

/* ============================================================
 * FreeRTOS/task 宏抽象
 * ============================================================ */
#ifdef CONFIG_FREERTOS
    #include "FreeRTOS.h"
    #include "task.h"
    #include "queue.h"
    #include "semphr.h"

    #define AXK_TASK_CREATE(func, name, stack, param, prio, handle) \
        xTaskCreate(func, name, stack, param, prio, handle)

    #define AXK_TASK_DELAY_MS(ms) \
        vTaskDelay(pdMS_TO_TICKS(ms))

    #define AXK_QUEUE_CREATE(len, size) \
        xQueueCreate(len, size)

    #define AXK_MUTEX_CREATE() \
        xSemaphoreCreateMutex()
#else
    /* 无RTOS时empty 实现 */
    #define AXK_TASK_CREATE(func, name, stack, param, prio, handle) (-1)
    #define AXK_TASK_DELAY_MS(ms) AXK_DELAY_MS(ms)
    #define AXK_QUEUE_CREATE(len, size) NULL
    #define AXK_MUTEX_CREATE() NULL
#endif

/* ============================================================
 * WiFi 宏抽象 - 基于BL618 wl80211驱动
 * ============================================================ */
#ifdef CONFIG_BL616_WIFI
    /* WiFimode  */
    #define AXK_WIFI_MODE_STA       0
    #define AXK_WIFI_MODE_AP        1
    #define AXK_WIFI_MODE_APSTA     2

    /* WiFiconnectstatus  */
    #define AXK_WIFI_STATUS_IDLE        0
    #define AXK_WIFI_STATUS_CONNECTING  1
    #define AXK_WIFI_STATUS_CONNECTED   2
    #define AXK_WIFI_STATUS_FAILED      3

    /* WiFi宏API - 简化call  */
    #define AXK_WIFI_INIT(mode) \
        axk_bl618_wifi_init_impl(mode)

    #define AXK_WIFI_CONNECT(ssid, pass) \
        axk_bl618_wifi_connect_impl(ssid, pass)

    #define AXK_WIFI_DISCONNECT() \
        axk_bl618_wifi_disconnect_impl()

    #define AXK_WIFI_IS_CONNECTED() \
        axk_bl618_wifi_is_connected_impl()
#endif

/* ============================================================
 * OTA 升级 宏抽象
 * ============================================================ */
#define AXK_OTA_START(url) \
    axk_bl618_ota_start_impl(url)

#define AXK_OTA_GET_PROGRESS() \
    axk_bl618_ota_get_progress_impl()

/* ============================================================
 * 平台特定func forward decl（internal实现用）
 * ============================================================ */
#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_BL616_WIFI
    /* @brief TODO: 描述axk_bl618_wifi_init_impl的功能 @param mode TODO: 描述mode @return 0成功, -1失败 */
    int axk_bl618_wifi_init_impl(int mode);
    /* @brief TODO: 描述axk_bl618_wifi_connect_impl的功能 @param ssid TODO: 描述ssid @param password TODO: 描述password @return 0成功, -1失败 */
    int axk_bl618_wifi_connect_impl(const char* ssid, const char* password);
    /* @brief TODO: 描述axk_bl618_wifi_disconnect_impl的功能 @return 0成功, -1失败 */
    int axk_bl618_wifi_disconnect_impl(void);
    /* @brief TODO: 描述axk_bl618_wifi_is_connected_impl的功能 @return 0成功, -1失败 */
    int axk_bl618_wifi_is_connected_impl(void);
#endif

    /* @brief TODO: 描述axk_bl618_ota_start_impl的功能 @param url TODO: 描述url @return 0成功, -1失败 */
    int axk_bl618_ota_start_impl(const char* url);
    /* @brief TODO: 描述axk_bl618_ota_get_progress_impl的功能 @return 0成功, -1失败 */
    int axk_bl618_ota_get_progress_impl(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_PLATFORM_BL618_H */
