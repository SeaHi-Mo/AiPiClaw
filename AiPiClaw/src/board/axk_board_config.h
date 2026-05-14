/**
 * @file axk_board_config.h
 * @brief board.json 解析器 - 统一板卡配置访问接口
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note boards/board.json 经 bin2obj 嵌入固件 .rodata，启动时一次性解析
 */

#ifndef __AXK_BOARD_CONFIG_H
#define __AXK_BOARD_CONFIG_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AXK_BOARD_NAME_MAX      32
#define AXK_CHIP_MODEL_MAX      16
#define AXK_VERSION_MAX         32
#define AXK_POLARITY_MAX        16
#define AXK_BOARD_MAX_ALIASES   32
#define AXK_BOARD_MAX_ALLOWED   32
#define AXK_BOARD_JSON_MAX      4096

/**
 * @brief 板卡配置结构体（从 board.json 解析）
 */
typedef struct {
    char board_name[AXK_BOARD_NAME_MAX];
    char chip_model[AXK_CHIP_MODEL_MAX];
    int  chip_freq_mhz;
    int  sram_kb;
    int  psram_mb;
    int  flash_mb;
    char sdk_version[AXK_VERSION_MAX];
    char fw_version[AXK_VERSION_MAX];
    char led_polarity[AXK_POLARITY_MAX];   /**< "active_high" or "active_low" */
} axk_board_config_t;

/**
 * @brief GPIO 别名条目（从 board.json pinmap/leds/buttons 解析）
 */
typedef struct {
    char name[24];          /**< 别名（如 "green_led"） */
    uint8_t pin;            /**< GPIO 引脚号 */
    uint8_t active_level;   /**< 有效电平 (1=高有效, 0=低有效) */
    uint8_t flags;          /**< 权限标志: bit0=WRITE, bit1=READ */
    char desc[64];          /**< 描述文本 */
} axk_board_alias_entry_t;

/* ── API ──────────────────────────────────────────── */

/**
 * @brief 初始化板卡配置解析器（解析嵌入的 board.json）
 * @return 0 成功, -1 解析失败 (fallback 硬编码)
 */
int  axk_board_config_init(void);

/**
 * @brief 获取解析后的板卡配置结构体
 * @return 配置结构体指针（只读），未初始化返回 NULL
 */
const axk_board_config_t *axk_board_config_get(void);

/**
 * @brief 获取原始 JSON 文本（嵌入的 board.json 原文）
 * @return JSON 字符串指针（只读，≤4KB）
 */
const char *axk_board_config_get_json(void);

/**
 * @brief 获取 LED 引脚列表
 * @param[out] pins 输出引脚数组
 * @param[in]  max  数组最大容量
 * @return 实际引脚数量
 */
int  axk_board_config_get_led_pins(int *pins, int max);

/**
 * @brief 获取 GPIO 白名单引脚列表
 * @param[out] pins 输出引脚数组
 * @param[in]  max  数组最大容量
 * @return 实际引脚数量
 */
int  axk_board_config_get_allowed_pins(int *pins, int max);

/**
 * @brief 通过别名查找引脚号
 * @param[in]  name    别名（如 "green_led", "红灯"）
 * @param[out] pin_out 输出引脚号
 * @return 0 成功, -1 未找到
 */
int  axk_board_config_lookup_alias(const char *name, uint8_t *pin_out);

/**
 * @brief 获取别名总数
 * @return 别名数量
 */
int  axk_board_config_get_alias_count(void);

/**
 * @brief 获取别名条目列表（用于批量遍历）
 * @param[out] entries 输出条目数组
 * @param[in]  max     数组最大容量
 * @return 实际条目数量
 */
int  axk_board_config_get_aliases(axk_board_alias_entry_t *entries, int max);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_BOARD_CONFIG_H */
