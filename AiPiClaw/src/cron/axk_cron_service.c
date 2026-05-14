/**
 * @file axk_cron_service.c
 * @brief crontaskservice实现 - 基于 RTC time 戳poll 
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note support  EVERY(循环interval)  &  AT(一次性) 两种mode 
 */

#include "axk_cron_service.h"
#include "axk_platform.h"
#include "axk_message_bus.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "bflb_rtc.h"

#include "easyflash.h"

#define AXK_CRON_MAX_JOBS   8
#define AXK_CRON_TASK_STACK 2048
#define AXK_CRON_TASK_PRIO  4
#define CRON_KV_KEY         "mimi_cron_jobs"

static cron_job_t s_jobs[AXK_CRON_MAX_JOBS];
static int s_job_count = 0;
static TaskHandle_t s_cron_task = NULL;
static bool s_cron_running = false;
static SemaphoreHandle_t s_cron_mutex = NULL;

/**
 * @brief get current  UTC time 戳（s）
 */
static int64_t axk_cron_now(void)
{
    return (int64_t)bflb_rtc_get_utc_timestamp();
}

/**
 * @brief will current crontasksave  to  easyflash KV
 */
static void cron_save_jobs(void)
{
    cJSON *root;
    cJSON *arr;
    int i;

    if (!s_cron_mutex) {
        return;
    }

    if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return;
    }

    root = cJSON_CreateObject();
    arr = cJSON_CreateArray();

    for (i = 0; i < AXK_CRON_MAX_JOBS; i++) {
        if (s_jobs[i].id[0] == '\0') {
            continue;
        }
        cJSON *j = cJSON_CreateObject();
        cJSON_AddStringToObject(j, "id", s_jobs[i].id);
        cJSON_AddStringToObject(j, "name", s_jobs[i].name);
        cJSON_AddNumberToObject(j, "kind", s_jobs[i].kind);
        cJSON_AddNumberToObject(j, "interval_s", s_jobs[i].interval_s);
        cJSON_AddNumberToObject(j, "at_epoch", (double)s_jobs[i].at_epoch);
        cJSON_AddStringToObject(j, "message", s_jobs[i].message);
        cJSON_AddStringToObject(j, "channel", s_jobs[i].channel);
        cJSON_AddStringToObject(j, "chat_id", s_jobs[i].chat_id);
        cJSON_AddNumberToObject(j, "enabled", s_jobs[i].enabled ? 1 : 0);
        cJSON_AddNumberToObject(j, "delete_after_run", s_jobs[i].delete_after_run ? 1 : 0);
        cJSON_AddNumberToObject(j, "next_run", (double)s_jobs[i].next_run);
        cJSON_AddItemToArray(arr, j);
    }

    cJSON_AddItemToObject(root, "jobs", arr);
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_str) {
        ef_set_env(CRON_KV_KEY, json_str);
        ef_save_env();
        vPortFree(json_str);
    }

    xSemaphoreGive(s_cron_mutex);
}

/**
 * @brief from  easyflash KV loadcrontask
 */
static void cron_load_jobs(void)
{
    char *json_str = NULL;
    const char *val = ef_get_env(CRON_KV_KEY);
    cJSON *root;
    cJSON *arr;
    int i;
    int count = 0;

    if (!val || val[0] == '\0') {
        return;
    }

    json_str = strdup(val);
    if (!json_str) {
        return;
    }

    root = cJSON_Parse(json_str);
    vPortFree(json_str);
    if (!root) {
        return;
    }

    arr = cJSON_GetObjectItem(root, "jobs");
    if (!cJSON_IsArray(arr)) {
        cJSON_Delete(root);
        return;
    }

    int n = cJSON_GetArraySize(arr);
    for (i = 0; i < n && i < AXK_CRON_MAX_JOBS; i++) {
        cJSON *j = cJSON_GetArrayItem(arr, i);
        if (!j) continue;

        cJSON *id = cJSON_GetObjectItem(j, "id");
        cJSON *name = cJSON_GetObjectItem(j, "name");
        cJSON *kind = cJSON_GetObjectItem(j, "kind");
        cJSON *interval_s = cJSON_GetObjectItem(j, "interval_s");
        cJSON *at_epoch = cJSON_GetObjectItem(j, "at_epoch");
        cJSON *message = cJSON_GetObjectItem(j, "message");
        cJSON *channel = cJSON_GetObjectItem(j, "channel");
        cJSON *chat_id = cJSON_GetObjectItem(j, "chat_id");
        cJSON *enabled = cJSON_GetObjectItem(j, "enabled");
        cJSON *delete_after_run = cJSON_GetObjectItem(j, "delete_after_run");
        cJSON *next_run = cJSON_GetObjectItem(j, "next_run");

        if (!cJSON_IsString(id) || !cJSON_IsString(name) || !cJSON_IsString(message)) {
            continue;
        }

        strncpy(s_jobs[i].id, id->valuestring, sizeof(s_jobs[i].id) - 1);
        strncpy(s_jobs[i].name, name->valuestring, sizeof(s_jobs[i].name) - 1);
        s_jobs[i].kind = cJSON_IsNumber(kind) ? kind->valueint : CRON_KIND_EVERY;
        s_jobs[i].interval_s = cJSON_IsNumber(interval_s) ? interval_s->valueint : 0;
        s_jobs[i].at_epoch = cJSON_IsNumber(at_epoch) ? (int64_t)at_epoch->valuedouble : 0;
        strncpy(s_jobs[i].message, message->valuestring, sizeof(s_jobs[i].message) - 1);
        if (cJSON_IsString(channel)) {
            strncpy(s_jobs[i].channel, channel->valuestring, sizeof(s_jobs[i].channel) - 1);
        }
        if (cJSON_IsString(chat_id)) {
            strncpy(s_jobs[i].chat_id, chat_id->valuestring, sizeof(s_jobs[i].chat_id) - 1);
        }
        s_jobs[i].enabled = (cJSON_IsNumber(enabled) && enabled->valueint != 0) ? true : false;
        s_jobs[i].delete_after_run = (cJSON_IsNumber(delete_after_run) && delete_after_run->valueint != 0) ? true : false;
        s_jobs[i].last_run = 0;
        s_jobs[i].next_run = cJSON_IsNumber(next_run) ? (int64_t)next_run->valuedouble : axk_cron_now();
        count++;
    }

    s_job_count = count;
    cJSON_Delete(root);
    AXK_LOG_INFO("[axk_cron] from  KV load %d crontask\r\n", count);
}

/**
 * @brief 生成 8 chars 十六进制task ID
 */
static void axk_cron_gen_id(char *id_buf, size_t len)
{
    static uint32_t s_counter = 0;
    uint32_t val = (uint32_t)axk_cron_now() + s_counter++;
    snprintf(id_buf, len, "%08X", (unsigned int)(val & 0xFFFFFFFF));
}

/**
 * @brief 初始化cron定时任务服务：创建互斥锁并从EasyFlash KV存储加载持久化的定时任务
 *
 * @return 0成功, -1失败
 */
int axk_cron_service_init(void)
{
    memset(s_jobs, 0, sizeof(s_jobs));
    s_job_count = 0;
    s_cron_running = false;
    if (s_cron_task) {
        vTaskDelete(s_cron_task);
        s_cron_task = NULL;
    }
    if (s_cron_mutex) {
        vSemaphoreDelete(s_cron_mutex);
    }
    s_cron_mutex = xSemaphoreCreateMutex();
    if (!s_cron_mutex) {
        AXK_LOG_ERROR("[axk_cron] createmutex FAIL\r\n");
        return -1;
    }
    cron_load_jobs();
    AXK_LOG_INFO("[axk_cron] crontaskserviceinitok\r\n");
    return 0;
}

/**
 * @brief 停止cron定时任务服务：终止后台FreeRTOS轮询任务
 *
 * @return 无返回值
 */
void axk_cron_service_stop(void)
{
    s_cron_running = false;
    if (s_cron_task) {
        vTaskDelete(s_cron_task);
        s_cron_task = NULL;
    }
    AXK_LOG_INFO("[axk_cron] crontaskservicestop \r\n");
}

/**
 * @brief 添加一个新的定时任务到任务列表，生成唯一ID并持久化到EasyFlash KV存储
 *
 * @param job 待添加的定时任务结构体指针（输入输出参数，函数将回填生成的唯一任务ID）
 * @return 0成功, -1失败（任务数已满或参数无效）
 */
int axk_cron_add_job(cron_job_t *job)
{
    int i;
    cron_job_t *slot;

    if (!job || !s_cron_mutex) {
        return -1;
    }

    if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    if (s_job_count >= AXK_CRON_MAX_JOBS) {
        xSemaphoreGive(s_cron_mutex);
        AXK_LOG_ERROR("[axk_cron] task数达上限\r\n");
        return -1;
    }

    for (i = 0; i < AXK_CRON_MAX_JOBS; i++) {
        if (s_jobs[i].id[0] == '\0') {
            slot = &s_jobs[i];
            break;
        }
    }
    if (i >= AXK_CRON_MAX_JOBS) {
        xSemaphoreGive(s_cron_mutex);
        return -1;
    }

    memcpy(slot, job, sizeof(cron_job_t));
    axk_cron_gen_id(slot->id, sizeof(slot->id));
    slot->enabled = true;
    slot->last_run = 0;

    int64_t now = axk_cron_now();
    if (slot->kind == CRON_KIND_EVERY) {
        slot->next_run = now + slot->interval_s;
    } else {
        slot->next_run = slot->at_epoch;
    }

    s_job_count++;
    xSemaphoreGive(s_cron_mutex);
    cron_save_jobs();
    AXK_LOG_INFO("[axk_cron] 添加task %s: %s\r\n", slot->id, slot->name);
    return 0;
}

/**
 * @brief 根据任务ID从任务列表中删除指定定时任务并更新持久化存储
 *
 * @param job_id 要删除的任务ID字符串（8位十六进制）
 * @return 0成功, -1失败（未找到匹配任务）
 */
int axk_cron_remove_job(const char *job_id)
{
    int i;

    if (!job_id || !s_cron_mutex) {
        return -1;
    }

    if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return -1;
    }

    for (i = 0; i < AXK_CRON_MAX_JOBS; i++) {
        if (strcmp(s_jobs[i].id, job_id) == 0) {
            AXK_LOG_INFO("[axk_cron] delete task %s\r\n", s_jobs[i].id);
            s_jobs[i].id[0] = '\0';  /* 标记为空；bus 通过 strdup 拷贝 message，安全释放 */
            s_job_count--;
            xSemaphoreGive(s_cron_mutex);
            cron_save_jobs();
            return 0;
        }
    }

    xSemaphoreGive(s_cron_mutex);
    return -1;
}

/**
 * @brief 获取当前所有定时任务的列表引用（返回内部数组指针，仅用于只读遍历）
 *
 * @param jobs 输出参数，接收指向内部任务数组的指针
 * @param count 输出参数，接收当前有效任务数量
 * @return 无返回值
 */
void axk_cron_list_jobs(const cron_job_t **jobs, int *count)
{
    if (!s_cron_mutex) {
        if (jobs) *jobs = NULL;
        if (count) *count = 0;
        return;
    }

    if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        if (jobs) *jobs = NULL;
        if (count) *count = 0;
        return;
    }

    if (jobs) {
        *jobs = s_jobs;
    }
    if (count) {
        *count = s_job_count;
    }
    xSemaphoreGive(s_cron_mutex);
}

/**
 * @brief 对crontask目标channel 进行消毒，ensure 有效 chat_id
 *
 * @note 果 channel 为empty then default 为 system；果is telegram but 缺少有效 chat_idthen 回退 to  system
 */
void axk_cron_sanitize_destination(cron_job_t *job)
{
    if (!job) {
        return;
    }

    if (job->channel[0] == '\0') {
        strncpy(job->channel, MIMI_CHAN_SYSTEM, sizeof(job->channel) - 1);
    }

    if (strcmp(job->channel, MIMI_CHAN_TELEGRAM) == 0 &&
        (job->chat_id[0] == '\0' || strcmp(job->chat_id, "cron") == 0)) {
        strncpy(job->channel, MIMI_CHAN_SYSTEM, sizeof(job->channel) - 1);
        job->chat_id[0] = '\0';
    }
}

/**
 * @brief 执行单task：will msg推入message bus
 */
static void axk_cron_execute_job(cron_job_t *job)
{
    mimi_msg_t msg;

    if (!job || job->message[0] == '\0') {
        return;
    }

    memset(&msg, 0, sizeof(msg));
    strncpy(msg.channel, job->channel[0] ? job->channel : MIMI_CHAN_SYSTEM, sizeof(msg.channel) - 1);
    strncpy(msg.chat_id, job->chat_id[0] ? job->chat_id : "cron", sizeof(msg.chat_id) - 1);
    msg.content = job->message;
    msg.priority = MIMI_PRIO_LOW;
    /* bus push_inbound 内部 strdup 拷贝 content，调用方保有所有权 */
    if (axk_message_bus_push_inbound(&msg) != 0) {
        AXK_LOG_WARN("[axk_cron] msg推入FAIL\r\n");
    }

    AXK_LOG_INFO("[axk_cron] 执行task %s: %s\r\n", job->id, job->name);
}

/**
 * @brief crontaskpoll task
 */
static void axk_cron_task(void *param)
{
    (void)param;
    int i;
    int64_t now;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!s_cron_running) {
            continue;
        }

        if (!s_cron_mutex) {
            continue;
        }

        if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }

        now = axk_cron_now();

        for (i = 0; i < AXK_CRON_MAX_JOBS; i++) {
            cron_job_t *job = &s_jobs[i];

            if (job->id[0] == '\0' || !job->enabled) {
                continue;
            }

            if (now >= job->next_run) {
                /* 先release 锁再执行task，避免in message bus操作持锁 */
                xSemaphoreGive(s_cron_mutex);
                axk_cron_execute_job(job);

                /* 重新get 锁update taskstatus  */
                if (xSemaphoreTake(s_cron_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
                    break;
                }
                job->last_run = now;

                if (job->kind == CRON_KIND_EVERY) {
                    job->next_run = now + job->interval_s;
                } else {
                    /* AT mode ：执行后标记delete  */
                    if (job->delete_after_run && job->id[0] != '\0') {
                        job->id[0] = '\0';  /* 仅标记为空，保持msg buffer有效 */
                        s_job_count--;
                    }
                }
            }
        }

        xSemaphoreGive(s_cron_mutex);
    }
}

/**
 * @brief startcrontaskpoll task
 *
 * @note in  axk_cron_service_start after 由主modulecall 
 */
int axk_cron_service_start(void)
{
    if (s_cron_task) {
        return 0;
    }

    s_cron_running = true;

    if (xTaskCreate(axk_cron_task, "cron", AXK_CRON_TASK_STACK, NULL, AXK_CRON_TASK_PRIO, &s_cron_task) != pdPASS) {
        s_cron_task = NULL;
        s_cron_running = false;
        AXK_LOG_ERROR("[axk_cron] createpoll taskFAIL\r\n");
        return -1;
    }

    AXK_LOG_INFO("[axk_cron] crontaskservicestarted\r\n");
    return 0;
}
