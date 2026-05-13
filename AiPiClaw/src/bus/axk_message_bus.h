/**
 * @file axk_message_bus.h
 * @brief message busmodule - 三级priority queue
 * @version 2.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note provide 双 to 三级priority queue，connect各channel &Agent Loop
 *       
 * TODO v2: 反压机制 (backpressure) — queuefull 时阻塞生产者而非drop 
 * TODO v2: 死信queue — drop msg记录 to 死信queuefor 诊断
 */

#ifndef __AXK_MESSAGE_BUS_H
#define __AXK_MESSAGE_BUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── channel 标识符 ─────────────────────────────────── */

#define MIMI_CHAN_TELEGRAM   "telegram"
#define MIMI_CHAN_FEISHU     "feishu"
#define MIMI_CHAN_WEBSOCKET  "websocket"
#define MIMI_CHAN_CLI        "cli"
#define MIMI_CHAN_SYSTEM     "system"

/* ── msgpriority  ─────────────────────────────────── */

typedef enum {
    MIMI_PRIO_LOW    = 0,  /**< 低priority : heartbeat、status report */
    MIMI_PRIO_NORMAL = 1,  /**< 普通priority : usermsg(default ) */
    MIMI_PRIO_HIGH   = 2,  /**< 高priority : system告警、OTAnotify */
} mimi_priority_t;

#define MIMI_PRIO_COUNT  3   /**< priority level 数 */

/* ── msg结构体 ─────────────────────────────────── */

typedef struct {
    char            channel[16];  /**< channel name : telegram/feishu/websocket/cli/system */
    char            chat_id[96];  /**< 聊天ID: Telegram chat_id、Feishu open_id、WS client id */
    char           *content;      /**< 堆alloc msg文本（call 者负责release ） */
    mimi_priority_t priority;     /**< msgpriority */
    bool            is_error;     /**< 是否为错误消息（LLM调用失败/Sorry回退=true，正常回复=false） */
} mimi_msg_t;

/* ── API ────────────────────────────────────────── */

/**
 * @brief initmessage bus（create3级inbound + 3级outbound FreeRTOSqueue）
 *
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_message_bus_init(void);

/**
 * Set task handles for Task Notification wake-up
 */
/**
 * @brief 设置入站消息消费者任务句柄，pop_inbound收到消息时通过Task Notification唤醒该任务
 *
 * @param task 消费者FreeRTOS任务句柄
 */
void axk_message_bus_set_inbound_consumer(TaskHandle_t task);
/**
 * @brief 设置出站消息消费者任务句柄，push_outbound成功后通过Task Notification唤醒该任务
 *
 * @param task 消费者FreeRTOS任务句柄
 */
void axk_message_bus_set_outbound_consumer(TaskHandle_t task);

/**
 * @brief will msg推入inboundqueue（通 to Agent Loop）
 *
 * @param[in] msg msgptr （priority字段决定queuelevel ）
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_message_bus_push_inbound(const mimi_msg_t *msg);

/**
 * @brief from inboundqueue弹出msg（非阻塞，高priority 优先）
 *
 * @param[out] msg output msg结构
 * @param[in] timeout_ms timeouttime （毫s），UINT32_MAX表示永久阻塞
 * @return OKreturn 0，timeoutreturn -1
 */
int axk_message_bus_pop_inbound(mimi_msg_t *msg, uint32_t timeout_ms);

/**
 * @brief will msg推入outboundqueue（通 to 各channel ）
 *
 * @param[in] msg msgptr 
 * @return OKreturn 0，FAILreturn 非零
 */
int axk_message_bus_push_outbound(const mimi_msg_t *msg);

/**
 * @brief from outboundqueue弹出msg（非阻塞，高priority 优先）
 *
 * @param[out] msg output msg结构
 * @param[in] timeout_ms timeouttime （毫s）
 * @return OKreturn 0，timeoutreturn -1
 */
int axk_message_bus_pop_outbound(mimi_msg_t *msg, uint32_t timeout_ms);

/**
 * @brief poll message bus
 */
void axk_message_bus_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MESSAGE_BUS_H */
