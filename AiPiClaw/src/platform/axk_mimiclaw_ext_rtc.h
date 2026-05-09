/**
 * @file axk_mimiclaw_ext_rtc.h
 * @brief \u5916\u90e8RTC\u6a21\u5757 - \u5b89\u4fe1\u53ef\u79d1\u6280 BL618 \u79fb\u690d\u7248
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 \u5b89\u4fe1\u53ef\u79d1\u6280\u6709\u9650\u516c\u53f8
 */

#ifndef __AXK_MIMICLAW_EXT_RTC_H
#define __AXK_MIMICLAW_EXT_RTC_H

#include <stdint.h>
#include <stdbool.h>
#include <bflb_rtc.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_ext_rtc_init(void);
bool axk_mimiclaw_ext_rtc_is_available(void);
bool axk_mimiclaw_ext_rtc_read_utc(struct bflb_tm *tm);
bool axk_mimiclaw_ext_rtc_write_utc(const struct bflb_tm *tm);
void axk_ext_rtc_set_time(uint8_t year, uint8_t month, uint8_t date,
                           uint8_t hour, uint8_t min, uint8_t sec);
void axk_ext_rtc_get_time(uint8_t *year, uint8_t *month, uint8_t *date,
                           uint8_t *hour, uint8_t *min, uint8_t *sec);
int16_t axk_ext_rtc_read_temperature(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MIMICLAW_EXT_RTC_H */
