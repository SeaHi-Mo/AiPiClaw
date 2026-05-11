/**
 * @file axk_mimiclaw_ext_rtc.h
 * @brief 外部RTC模块 - 安信可科技 BL618 移植版
 * @details 封装BL618内部RTC外设(bflb_rtc)，提供UTC时间读写、温度读取功能。
 *          BL618内置RTC通过bflb_rtc_init()初始化，支持年月日时分秒字段读写，
 *          以及芯片温度传感器读取。
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 安信可科技有限公司
 */
#ifndef __AXK_MIMICLAW_EXT_RTC_H
#define __AXK_MIMICLAW_EXT_RTC_H

#include <stdint.h>
#include <stdbool.h>
/** @brief BL618 RTC外设头文件 */
#include <bflb_rtc.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化外部RTC模块
 * @return 0成功，负数错误码
 */
int axk_ext_rtc_init(void);
/**
 * @brief 检查RTC是否可用
 * @return true=可用，false=未初始化或故障
 */
bool axk_mimiclaw_ext_rtc_is_available(void);
/**
 * @brief 读取UTC时间
 * @param tm[out] 输出的时间结构体
 * @return true=读取成功，false=失败
 */
bool axk_mimiclaw_ext_rtc_read_utc(struct bflb_tm *tm);
/**
 * @brief 写入UTC时间
 * @param tm[in] 要设置的时间结构体
 * @return true=写入成功，false=失败
 */
bool axk_mimiclaw_ext_rtc_write_utc(const struct bflb_tm *tm);
/**
 * @brief 设置RTC时间（分解为字段）
 * @param year  年（后两位，如24表示2024）
 * @param month 月（1-12）
 * @param date  日（1-31）
 * @param hour  时（0-23）
 * @param min   分（0-59）
 * @param sec   秒（0-59）
 */
void axk_ext_rtc_set_time(uint8_t year, uint8_t month, uint8_t date,
                           uint8_t hour, uint8_t min, uint8_t sec);
/**
 * @brief 读取RTC时间（分解为字段）
 * @param year[out]  年
 * @param month[out] 月
 * @param date[out]  日
 * @param hour[out]  时
 * @param min[out]   分
 * @param sec[out]   秒
 */
void axk_ext_rtc_get_time(uint8_t *year, uint8_t *month, uint8_t *date,
                           uint8_t *hour, uint8_t *min, uint8_t *sec);
/**
 * @brief 读取芯片温度
 * @return 温度值（摄氏度 × 10），如250表示25.0°C。
 *         负数表示错误。
 * @note BL618内部温度传感器，精度约±2°C
 */
int16_t axk_ext_rtc_read_temperature(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MIMICLAW_EXT_RTC_H */
