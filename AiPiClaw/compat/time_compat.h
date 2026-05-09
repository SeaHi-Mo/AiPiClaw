/**
 * @file time_compat.h
 * @brief BL618 time() 兼容层 - bflb_rtc 替代标准C time()
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_COMPAT_TIME_H
#define __AXK_COMPAT_TIME_H

#include <time.h>
#include "bflb_rtc.h"

/** BL618 平台用 bflb_rtc 替代标准 time() */
#define time(x) ((time_t)bflb_rtc_get_utc_timestamp())

#endif
