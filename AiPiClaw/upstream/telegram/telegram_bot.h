#pragma once


/**
 * Initialize the Telegram bot.
 */
int axk_telegram_bot_init(void);

/**
 * Start the Telegram polling task (long polling on Core 0).
 */
int axk_telegram_bot_start(void);

/**
 * Send a text message to a Telegram chat.
 * Automatically splits messages longer than 4096 chars.
 * @param chat_id  Telegram chat ID (numeric string)
 * @param text     Message text (supports Markdown)
 */
int axk_telegram_send_message(const char *chat_id, const char *text);

/**
 * Save the Telegram bot token to NVS.
 */
int axk_telegram_set_token(const char *token);

