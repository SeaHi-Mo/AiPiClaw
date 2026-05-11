/**
 * @file axk_cron_service.h
 * @brief crontaskservice - 安信可科技 BL618 port
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 整合自官方 solution/mimiclaw/port
 */

#ifndef __AXK_CRON_SERVICE_H
#define __AXK_CRON_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 调度type  */
typedef enum {
    CRON_KIND_EVERY = 0,   /* 循环interval，单位s */
    CRON_KIND_AT    = 1,   /* 一次性，Unixtime 戳 */
} cron_kind_t;

/* 单crontask */
typedef struct {
    char id[9];            /* 8chars 十六进制ID + null */
    char name[32];                       /**< 任务名称 */
    bool enabled;                          /**< 是否启用 */
    cron_kind_t kind;                       /**< 定时类型（EVERY=循环, AT=一次性） */
    uint32_t interval_s;   /* EVERYmode : intervals数 */
    int64_t at_epoch;      /* ATmode : Unixtime 戳 */
    char message[256];     /* 注入msgqueuemsg */
    char channel[16];      /* 回复channel  (default  "system") */
    char chat_id[96];      /* 回复chat_id/open_id (default  "cron") */
    int64_t last_run;      /* 上次执行time  */
    int64_t next_run;      /* 下次执行time  */
    bool delete_after_run; /* 执行后delete  (for ATtask) */
} cron_job_t;

/**
 * @brief initcrontaskservice，from SPIFFSloadtask
 *
 * @return OKreturn 0
 */
int axk_cron_service_init(void);

/**
 * @brief startcrontask计时器，建议in WiFiconnect and sync time 后call 
 *
 * @return OKreturn 0
 */
int axk_cron_service_start(void);

/**
 * @brief stop crontaskservice
 */
void axk_cron_service_stop(void);

/**
 * @brief 添加新crontask
 *
 * @param[in] job task结构体ptr （idwill auto 生成）
 * @return OKreturn 0，task数达上限return 非零
 */
int axk_cron_add_job(cron_job_t *job);

/**
 * @brief per IDdelete crontask
 *
 * @param[in] job_id 8chars taskID
 * @return OKreturn 0，not 找 to return 非零
 */
int axk_cron_remove_job(const char *job_id);

/**
 * @brief 列出allcrontask
 *
 * @param[out] jobs output task数组
 * @param[out] count taskcount 
 */
void axk_cron_list_jobs(const cron_job_t **jobs, int *count);

/**
 * @brief 对crontask目标channel 进行消毒，ensure 有效 chat_id
 *
 * @param[in,out] job task结构体ptr 
 */
void axk_cron_sanitize_destination(cron_job_t *job);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_CRON_SERVICE_H */
