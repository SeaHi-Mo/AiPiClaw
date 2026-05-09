/**
 * @file axk_telegram_bot.h
 * @brief Telegram Bot module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_TELEGRAM_BOT_H
#define __AXK_TELEGRAM_BOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief initTelegram Bot
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_telegram_bot_init(void);

/**
 * @brief startTelegrampoll task
 * @return OKreturn 0
 */
int axk_telegram_bot_start(void);

/**
 * @brief send文本msg to Telegram聊天
 * @param[in] chat_id 聊天ID
 * @param[in] text msgcontent 
 * @return OKreturn 0
 */
int axk_telegram_send_message(const char *chat_id, const char *text);

/**
 * @brief save Telegram Bot Token to NVS
 * @param[in] token Bot Token
 * @return OKreturn 0
 */
int axk_telegram_set_token(const char *token);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_TELEGRAM_BOT_H */
