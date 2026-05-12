/**
 * @file axk_feishu_bot.h
 * @brief Feishu 飞书 Bot module - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#ifndef __AXK_FEISHU_BOT_H
#define __AXK_FEISHU_BOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief initFeishu Bot
 *
 * @return OKreturn 0
 */
int axk_feishu_bot_init(void);

/**
 * @brief startFeishu事件recvservice
 *
 * @return OKreturn 0
 */
int axk_feishu_bot_start(void);

/**
 * @brief sendmsg to Feishu
 *
 * @param[in] chat_id 聊天ID
 * @param[in] text msgcontent 
 * @return OKreturn 0
 */
int axk_feishu_send_message(const char *chat_id, const char *text);

/**
 * @brief set Feishu应用credential 
 *
 * @param[in] app_id 应用ID
 * @param[in] app_secret 应用key 
 * @return OKreturn 0
 */
int axk_feishu_set_credentials(const char *app_id, const char *app_secret);

/**
 * @brief 启动飞书 Webhook 接收服务
 *
 * @return 0成功，负数错误码
 */
/**
 * @brief 停止飞书Bot服务
 */
void axk_feishu_bot_stop(void);

/**
 * @brief 停止飞书Webhook接收服务
 */
void axk_feishu_bot_stop_webhook(void);

int axk_feishu_bot_start_webhook(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_FEISHU_BOT_H */
