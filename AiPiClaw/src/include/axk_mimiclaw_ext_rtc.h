/**
 * @file axk_mimiclaw_ext_rtc.h
 * @brief external RTC接口 (DS3231) - 安信可科技 BL618 port
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef AXK_MIMICLAW_EXT_RTC_H
#define AXK_MIMICLAW_EXT_RTC_H

#include <stdbool.h>
#include "bflb_rtc.h"

#ifdef __cplusplus
extern "C" {
#endif

/* @brief TODO: 描述axk_ext_rtc_init的功能 @return 0成功, -1失败 */
int axk_ext_rtc_init(void);
__attribute__((weak)) bool axk_mimiclaw_ext_rtc_is_available(void);
__attribute__((weak)) bool axk_mimiclaw_ext_rtc_read_utc(struct bflb_tm *tm);
__attribute__((weak)) bool axk_mimiclaw_ext_rtc_write_utc(const struct bflb_tm *tm);

#ifdef __cplusplus
}
#endif

#endif
