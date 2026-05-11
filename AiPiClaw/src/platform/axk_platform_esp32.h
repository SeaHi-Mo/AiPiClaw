/**
 * @file axk_platform_esp32.h
 * @brief ESP32平台特定实现 - 宏元编程封装层
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note will ESP-IDF API封装为平台无关接口，保留原始MimiClaw代码兼容性
 *
 * @par use 方法
 * 定义 CONFIG_AXK_PLATFORM_ESP32 后include 此file，
 * 原MimiClaw代码 esp_xxx() call will via 宏映射为统一接口。
 */

#ifndef __AXK_PLATFORM_ESP32_H
#define __AXK_PLATFORM_ESP32_H

/* ============================================================
 * ESP-IDF 头fileinclude 
 * ============================================================ */
#include "esp_system.h"
#include "axk_platform.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "nvs_flash.h"
#include "esp_spi_flash.h"

/* ============================================================
 * 平台init实现
 * ============================================================ */
#define AXK_PLATFORM_IMPL_INIT() \
    do { \
        int ret = nvs_flash_init(); \
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) { \
            ESP_ERROR_CHECK(nvs_flash_erase()); \
            ret = nvs_flash_init(); \
        } \
        ESP_ERROR_CHECK(ret); \
    } while(0)

/* ============================================================
 * 延时实现
 * ============================================================ */
#define AXK_PLATFORM_IMPL_DELAY_MS(ms)      vTaskDelay(pdMS_TO_TICKS(ms))
#define AXK_PLATFORM_IMPL_DELAY_US(us)      esp_rom_delay_us(us)

/* ============================================================
 * log 实现 - use ESP-IDFlog system
 * ============================================================ */
#define AXK_PLATFORM_IMPL_LOG(fmt, ...) \
    AXK_LOG_INFO("MimiClaw", fmt, ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_ERROR(fmt, ...) \
    AXK_LOG_ERROR("MimiClaw", fmt, ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_WARN(fmt, ...) \
    AXK_LOG_WARN("MimiClaw", fmt, ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_INFO(fmt, ...) \
    AXK_LOG_INFO("MimiClaw", fmt, ##__VA_ARGS__)

#define AXK_PLATFORM_IMPL_LOG_DEBUG(fmt, ...) \
    AXK_LOG_DEBUG("MimiClaw", fmt, ##__VA_ARGS__)

/* ============================================================
 * 断mgr实现 - FreeRTOS临界区
 * ============================================================ */
#define AXK_PLATFORM_IMPL_ENTER_CRITICAL() \
    portENTER_CRITICAL(&axk_esp32_spinlock)

#define AXK_PLATFORM_IMPL_EXIT_CRITICAL() \
    portEXIT_CRITICAL(&axk_esp32_spinlock)

/* 全局旋转锁定义 */
static portMUX_TYPE axk_esp32_spinlock = portMUX_INITIALIZER_UNLOCKED;

/* ============================================================
 * 内存屏障实现 - Xtensa指令
 * ============================================================ */
#define AXK_PLATFORM_IMPL_MEMORY_BARRIER() \
    __asm__ volatile ("memw" ::: "memory")

/* ============================================================
 * GPIO 宏抽象 - 封装ESP32 GPIO API
 * ============================================================ */

/* GPIO方 to 映射 */
#define AXK_GPIO_MODE_INPUT     GPIO_MODE_INPUT
#define AXK_GPIO_MODE_OUTPUT    GPIO_MODE_OUTPUT
#define AXK_GPIO_MODE_AF        GPIO_MODE_AF_OD
#define AXK_GPIO_MODE_ANALOG    GPIO_MODE_DISABLE

/* GPIOstatus  */
#define AXK_GPIO_LEVEL_LOW      0
#define AXK_GPIO_LEVEL_HIGH     1

/* GPIO宏操作 */
#define AXK_GPIO_INIT(pin, mode) \
    do { \
        gpio_config_t io_conf = {0}; \
        io_conf.pin_bit_mask = (1ULL << pin); \
        io_conf.mode = mode; \
        io_conf.pull_up_en = 0; \
        io_conf.pull_down_en = 0; \
        io_conf.intr_type = GPIO_INTR_DISABLE; \
        gpio_config(&io_conf); \
    } while(0)

#define AXK_GPIO_SET_LEVEL(pin, level) \
    gpio_set_level(pin, level)

#define AXK_GPIO_GET_LEVEL(pin) \
    gpio_get_level(pin)

#define AXK_GPIO_TOGGLE(pin) \
    gpio_set_level(pin, !gpio_get_level(pin))

/* ============================================================
 * UART 宏抽象 - 封装ESP32 UART API
 * ============================================================ */
#define AXK_UART_DEFAULT_BAUDRATE   115200
#define AXK_UART_DEFAULT_DEVICE     UART_NUM_0

/* UARTconfig宏 */
#define AXK_UART_CONFIG(uart_dev, baud) \
    do { \
        uart_config_t uart_cfg = {0}; \
        uart_cfg.baud_rate = baud; \
        uart_cfg.data_bits = UART_DATA_8_BITS; \
        uart_cfg.parity = UART_PARITY_DISABLE; \
        uart_cfg.stop_bits = UART_STOP_BITS_1; \
        uart_cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE; \
        uart_cfg.source_clk = UART_SCLK_DEFAULT; \
        uart_param_config(uart_dev, &uart_cfg); \
        uart_driver_install(uart_dev, 256, 256, 0, NULL, 0); \
    } while(0)

/* UARTdatasend宏 */
#define AXK_UART_PUTCHAR(uart_dev, ch) \
    uart_write_bytes(uart_dev, &(char){ch}, 1)

#define AXK_UART_GETCHAR(uart_dev) \
    axk_esp32_uart_getchar_impl(uart_dev)

/* ============================================================
 * Flash/store 宏抽象 - 封装SPI Flash API
 * ============================================================ */
#define AXK_FLASH_ERASE_CHIP()          esp_flash_erase_chip(NULL)
#define AXK_FLASH_ERASE_SECTOR(addr)    esp_flash_erase_region(NULL, addr, 4096)
#define AXK_FLASH_READ(addr, buf, len)  esp_flash_read(NULL, buf, addr, len)
#define AXK_FLASH_WRITE(addr, buf, len) esp_flash_write(NULL, buf, addr, len)

/* ============================================================
 * systemINFO 宏抽象
 * ============================================================ */
#define AXK_GET_CHIP_NAME()             "ESP32"
#define AXK_GET_SDK_VERSION()           "esp-idf-v5.x"
#define AXK_GET_CPU_FREQ_MHZ()          240
#define AXK_GET_HEAP_SIZE()             esp_get_free_heap_size()

/* ============================================================
 * FreeRTOS 宏抽象 - ESP-IDFdefault 集成FreeRTOS
 * ============================================================ */
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

/* ============================================================
 * WiFi 宏抽象 - 封装ESP-IDF WiFi API
 * ============================================================ */
#include "esp_wifi.h"

#define AXK_WIFI_MODE_STA       WIFI_MODE_STA
#define AXK_WIFI_MODE_AP        WIFI_MODE_AP
#define AXK_WIFI_MODE_APSTA     WIFI_MODE_APSTA

#define AXK_WIFI_STATUS_IDLE        0
#define AXK_WIFI_STATUS_CONNECTING  1
#define AXK_WIFI_STATUS_CONNECTED   2
#define AXK_WIFI_STATUS_FAILED      3

#define AXK_WIFI_INIT(mode) \
    axk_esp32_wifi_init_impl(mode)

#define AXK_WIFI_CONNECT(ssid, pass) \
    axk_esp32_wifi_connect_impl(ssid, pass)

#define AXK_WIFI_DISCONNECT() \
    axk_esp32_wifi_disconnect_impl()

#define AXK_WIFI_IS_CONNECTED() \
    axk_esp32_wifi_is_connected_impl()

/* ============================================================
 * OTA 升级 宏抽象 - 封装ESP32 OTA API
 * ============================================================ */
#include "esp_ota_ops.h"

#define AXK_OTA_START(url) \
    axk_esp32_ota_start_impl(url)

#define AXK_OTA_GET_PROGRESS() \
    axk_esp32_ota_get_progress_impl()

/* ============================================================
 * 平台特定func forward decl
 * ============================================================ */
#ifdef __cplusplus
extern "C" {
#endif

    /**
 * @brief TODO: 描述axk_esp32_uart_getchar_impl的功能
 *
 * @param uart_num TODO: 描述uart_num
 * @return 0成功, -1失败
 */
    int axk_esp32_uart_getchar_impl(int uart_num);
    /**
 * @brief TODO: 描述axk_esp32_wifi_init_impl的功能
 *
 * @param mode TODO: 描述mode
 * @return 0成功, -1失败
 */
    int axk_esp32_wifi_init_impl(int mode);
    /**
 * @brief TODO: 描述axk_esp32_wifi_connect_impl的功能
 *
 * @param ssid TODO: 描述ssid
 * @param password TODO: 描述password
 * @return 0成功, -1失败
 */
    int axk_esp32_wifi_connect_impl(const char* ssid, const char* password);
    /**
 * @brief TODO: 描述axk_esp32_wifi_disconnect_impl的功能
 *
 * @return 0成功, -1失败
 */
    int axk_esp32_wifi_disconnect_impl(void);
    /**
 * @brief TODO: 描述axk_esp32_wifi_is_connected_impl的功能
 *
 * @return 0成功, -1失败
 */
    int axk_esp32_wifi_is_connected_impl(void);
    /**
 * @brief TODO: 描述axk_esp32_ota_start_impl的功能
 *
 * @param url TODO: 描述url
 * @return 0成功, -1失败
 */
    int axk_esp32_ota_start_impl(const char* url);
    /**
 * @brief TODO: 描述axk_esp32_ota_get_progress_impl的功能
 *
 * @return 0成功, -1失败
 */
    int axk_esp32_ota_get_progress_impl(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_PLATFORM_ESP32_H */
