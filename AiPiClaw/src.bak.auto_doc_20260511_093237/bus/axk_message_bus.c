/**
 * @file axk_message_bus.c
 * @brief message bus实现 - 三级priority  FreeRTOS queue
 * @version 2.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note use  3  FreeRTOS queueresp. 存放高//低priority msg
 *       pop 按priority 降序非阻塞poll ，保证高priority msg优先process
 */

#include "axk_message_bus.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

/* queueconfig */
#define MIMI_BUS_QUEUE_LEN  16  /**< per 级queue最大msg数 */

/* message busstatus  */
static QueueHandle_t s_inbound[MIMI_PRIO_COUNT]  = {NULL};
static QueueHandle_t s_outbound[MIMI_PRIO_COUNT] = {NULL};
static TaskHandle_t s_inbound_consumer = NULL;   /**< agent_loop task to notify */
static TaskHandle_t s_outbound_consumer = NULL;  /**< main_loop task to notify */
static bool s_initialized = false;

/* ── internalhelper  ───────────────────────────────────── */

static const char *prio_name(mimi_priority_t p)
{
    switch (p) {
        case MIMI_PRIO_HIGH:   return "HIGH";
        case MIMI_PRIO_NORMAL: return "NORM";
        case MIMI_PRIO_LOW:    return "LOW";
        default:               return "???";
    }
}

/* ── public  API ────────────────────────────────────── */

/**
 * @brief initmessage bus
 * @return OKreturn 0
 */
int axk_message_bus_init(void)
{
    int i;

    if (s_initialized) {
        return 0;
    }

    for (i = 0; i < MIMI_PRIO_COUNT; i++) {
        s_inbound[i]  = xQueueCreate(MIMI_BUS_QUEUE_LEN, sizeof(mimi_msg_t));
        s_outbound[i] = xQueueCreate(MIMI_BUS_QUEUE_LEN, sizeof(mimi_msg_t));

        if (!s_inbound[i] || !s_outbound[i]) {
            AXK_LOG_ERROR("[axk_message_bus] queuecreateFAIL (prio=%d)\r\n", i);
            return -1;
        }
    }

    s_initialized = true;
    AXK_LOG_INFO("[axk_message_bus] message businitok (3级×%d, 6 queues total)\r\n",
                 MIMI_BUS_QUEUE_LEN);
    return 0;
}

void axk_message_bus_set_inbound_consumer(TaskHandle_t task)
{
    s_inbound_consumer = task;
}

void axk_message_bus_set_outbound_consumer(TaskHandle_t task)
{
    s_outbound_consumer = task;
}

/**
 * @brief will msg推入inboundqueue
 * @param[in] msg msgptr ，internal会 strdup content
 * @return OKreturn 0
 */
int axk_message_bus_push_inbound(const mimi_msg_t *msg)
{
    mimi_msg_t copy;
    mimi_priority_t prio;
    QueueHandle_t q;

    if (!s_initialized || !msg) {
        printf("[MSG_BUS] push_inbound: s_init=%d msg=%p\r\n", (int)s_initialized, (void*)msg);
        return -1;
    }

    prio = msg->priority;
    if ((int)prio < 0 || prio >= MIMI_PRIO_COUNT) {
        prio = MIMI_PRIO_NORMAL;
    }
    q = s_inbound[prio];

    copy = *msg;
    if (msg->content) {
        copy.content = strdup(msg->content);
        if (!copy.content) {
            return -1;
        }
    }

    if (xQueueSend(q, &copy, pdMS_TO_TICKS(100)) != pdTRUE) {
        AXK_LOG_WARN("[axk_message_bus] Inbound[%s] queuefull , drop msg\r\n",
                     prio_name(prio));
        free(copy.content);
        return -1;
    }

    if (s_inbound_consumer) {
        xTaskNotifyGive(s_inbound_consumer);  /* wake agent_loop */
    }
    return 0;
}

/**
 * @brief from inbound弹出msg（高priority 优先）
 * @param[out] msg output msg
 * @param[in] timeout_ms timeout(ms)
 * @return OKreturn 0
 */
int axk_message_bus_pop_inbound(mimi_msg_t *msg, uint32_t timeout_ms)
{
    TickType_t ticks;
    int prio;

    if (!s_initialized || !msg) {
        return -1;
    }

    ticks = (timeout_ms == (uint32_t)-1) ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms);

    /* Wait for Task Notification, then scan all queues non-blocking */
    if (ticks > 0) {
        ulTaskNotifyTake(pdTRUE, ticks);
    }
    for (prio = MIMI_PRIO_HIGH; prio >= MIMI_PRIO_LOW; prio--) {
        TickType_t t = 0;  /* non-blocking after notification wake */
        if (xQueueReceive(s_inbound[prio], msg, t) == pdTRUE) {
            msg->priority = (mimi_priority_t)prio;
            return 0;
        }
    }

    return -1;  /**< alllevel all无msg */
}

/**
 * @brief will msg推入outboundqueue
 * @param[in] msg msgptr 
 * @return OKreturn 0
 */
int axk_message_bus_push_outbound(const mimi_msg_t *msg)
{
    mimi_msg_t copy;
    mimi_priority_t prio;
    QueueHandle_t q;

    if (!s_initialized || !msg) {
        return -1;
    }

    prio = msg->priority;
    if ((int)prio < 0 || prio >= MIMI_PRIO_COUNT) {
        prio = MIMI_PRIO_NORMAL;
    }
    q = s_outbound[prio];

    copy = *msg;
    if (msg->content) {
        copy.content = strdup(msg->content);
        if (!copy.content) {
            return -1;
        }
    }

    if (xQueueSend(q, &copy, pdMS_TO_TICKS(100)) != pdTRUE) {
        AXK_LOG_WARN("[axk_message_bus] Outbound[%s] queuefull , drop msg\r\n",
                     prio_name(prio));
        free(copy.content);
        return -1;
    }

    if (s_outbound_consumer) {
        xTaskNotifyGive(s_outbound_consumer);  /* wake main_loop */
    }
    return 0;
}

/**
 * @brief from outbound弹出msg（高priority 优先）
 * @param[out] msg output msg
 * @param[in] timeout_ms timeout(ms)
 * @return OKreturn 0
 */
int axk_message_bus_pop_outbound(mimi_msg_t *msg, uint32_t timeout_ms)
{
    TickType_t ticks;
    int prio;

    if (!s_initialized || !msg) {
        return -1;
    }

    ticks = (timeout_ms == (uint32_t)-1) ? portMAX_DELAY
                                         : pdMS_TO_TICKS(timeout_ms);

    /* Wait for Task Notification, then scan all queues non-blocking */
    if (ticks > 0) {
        ulTaskNotifyTake(pdTRUE, ticks);
    }
    for (prio = MIMI_PRIO_HIGH; prio >= MIMI_PRIO_LOW; prio--) {
        TickType_t t = 0;
        if (xQueueReceive(s_outbound[prio], msg, t) == pdTRUE) {
            msg->priority = (mimi_priority_t)prio;
            return 0;
        }
    }

    return -1;
}

/**
 * @brief poll （FreeRTOSqueue驱动，here 为兼容占位）
 */
void axk_message_bus_poll(void)
{
    /* FreeRTOS queue由 xQueueReceive 驱动，无需主动poll  */
}
