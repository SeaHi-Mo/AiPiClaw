/**
 * @file axk_tool_cron.c
 * @brief crontasktool实现 - support  LLM via toolcall 来mgrcrontask
 * @version 1.0
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note provide  cron_add、cron_list、cron_remove 三toolfunc 
 */

#include "axk_tool_cron.h"
#include "axk_cron_service.h"
#include "axk_platform.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"

static const char *TAG = "tool_cron";

/**
 * @brief TODO: 描述axk_tool_cron_add_execute的功能
 *
 * @param input_json TODO: 描述input_json
 * @param output TODO: 描述output
 * @param output_size TODO: 描述output_size
 * @return 0成功, -1失败
 */
int axk_tool_cron_add_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    cJSON *name;
    cJSON *schedule_type;
    cJSON *interval_s;
    cJSON *at_epoch;
    cJSON *message;
    cJSON *channel;
    cJSON *chat_id;
    cJSON *delete_after_run;
    cron_job_t job;

    if (!input_json || !output || output_size == 0) {
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    name = cJSON_GetObjectItem(root, "name");
    schedule_type = cJSON_GetObjectItem(root, "schedule_type");
    message = cJSON_GetObjectItem(root, "message");

    if (!cJSON_IsString(name) || !name->valuestring ||
        !cJSON_IsString(schedule_type) || !schedule_type->valuestring ||
        !cJSON_IsString(message) || !message->valuestring) {
        snprintf(output, output_size, "Error: missing required fields (name, schedule_type, message)");
        cJSON_Delete(root);
        return -1;
    }

    memset(&job, 0, sizeof(job));
    strncpy(job.name, name->valuestring, sizeof(job.name) - 1);
    strncpy(job.message, message->valuestring, sizeof(job.message) - 1);

    /* parse 调度type  */
    if (strcmp(schedule_type->valuestring, "every") == 0) {
        job.kind = CRON_KIND_EVERY;
        interval_s = cJSON_GetObjectItem(root, "interval_s");
        if (!cJSON_IsNumber(interval_s) || interval_s->valueint <= 0) {
            snprintf(output, output_size, "Error: interval_s must be a positive integer for 'every' schedule");
            cJSON_Delete(root);
            return -1;
        }
        job.interval_s = (uint32_t)interval_s->valueint;
    } else if (strcmp(schedule_type->valuestring, "at") == 0) {
        job.kind = CRON_KIND_AT;
        at_epoch = cJSON_GetObjectItem(root, "at_epoch");
        if (!cJSON_IsNumber(at_epoch)) {
            snprintf(output, output_size, "Error: at_epoch must be a unix timestamp for 'at' schedule");
            cJSON_Delete(root);
            return -1;
        }
        job.at_epoch = (int64_t)at_epoch->valuedouble;
        job.delete_after_run = true;
    } else {
        snprintf(output, output_size, "Error: schedule_type must be 'every' or 'at'");
        cJSON_Delete(root);
        return -1;
    }

    /* 可选字段 */
    channel = cJSON_GetObjectItem(root, "channel");
    if (cJSON_IsString(channel) && channel->valuestring) {
        strncpy(job.channel, channel->valuestring, sizeof(job.channel) - 1);
    }

    chat_id = cJSON_GetObjectItem(root, "chat_id");
    if (cJSON_IsString(chat_id) && chat_id->valuestring) {
        strncpy(job.chat_id, chat_id->valuestring, sizeof(job.chat_id) - 1);
    }

    delete_after_run = cJSON_GetObjectItem(root, "delete_after_run");
    if (cJSON_IsBool(delete_after_run)) {
        job.delete_after_run = cJSON_IsTrue(delete_after_run);
    }

    /* 目标channel 消毒 */
    axk_cron_sanitize_destination(&job);

    if (axk_cron_add_job(&job) != 0) {
        snprintf(output, output_size, "Error: failed to add cron job (max %d jobs reached)", MIMI_CRON_MAX_JOBS);
        cJSON_Delete(root);
        return -1;
    }

    snprintf(output, output_size,
             "Cron job added successfully.\n"
             "ID: %s\n"
             "Name: %s\n"
             "Schedule: %s\n"
             "Channel: %s\n"
             "Chat ID: %s",
             job.id, job.name,
             job.kind == CRON_KIND_EVERY ? "every" : "at",
             job.channel, job.chat_id);

    cJSON_Delete(root);
    return 0;
}

/**
 * @brief TODO: 描述axk_tool_cron_list_execute的功能
 *
 * @param input_json TODO: 描述input_json
 * @param output TODO: 描述output
 * @param output_size TODO: 描述output_size
 * @return 0成功, -1失败
 */
int axk_tool_cron_list_execute(const char *input_json, char *output, size_t output_size)
{
    const cron_job_t *jobs;
    int count;
    int i;
    size_t off = 0;
    int n;

    (void)input_json;

    if (!output || output_size == 0) {
        return -1;
    }

    axk_cron_list_jobs(&jobs, &count);

    if (count == 0 || !jobs) {
        snprintf(output, output_size, "No cron jobs scheduled.");
        return 0;
    }

    n = snprintf(output + off, output_size - off,
                 "Scheduled cron jobs (%d):\n", count);
    if (n > 0) {
        off += (size_t)n;
    }

    for (i = 0; i < MIMI_CRON_MAX_JOBS; i++) {
        const cron_job_t *job = &jobs[i];
        if (job->id[0] == '\0') {
            continue;
        }

        n = snprintf(output + off, output_size - off,
                     "\n[%s] %s\n"
                     "  Enabled: %s\n"
                     "  Kind: %s\n"
                     "  Channel: %s\n"
                     "  Chat ID: %s\n"
                     "  Message: %.80s%s\n",
                     job->id,
                     job->name,
                     job->enabled ? "yes" : "no",
                     job->kind == CRON_KIND_EVERY ? "every" : "at",
                     job->channel,
                     job->chat_id,
                     job->message,
                     (strlen(job->message) > 80) ? "..." : "");
        if (n > 0) {
            off += (size_t)n;
        }

        if (job->kind == CRON_KIND_EVERY) {
            n = snprintf(output + off, output_size - off,
                         "  Interval: %lu seconds\n",
                         (unsigned long)job->interval_s);
        } else {
            n = snprintf(output + off, output_size - off,
                         "  At epoch: %lld\n  Delete after run: %s\n",
                         (long long)job->at_epoch,
                         job->delete_after_run ? "yes" : "no");
        }
        if (n > 0) {
            off += (size_t)n;
        }
    }

    return 0;
}

/**
 * @brief TODO: 描述axk_tool_cron_remove_execute的功能
 *
 * @param input_json TODO: 描述input_json
 * @param output TODO: 描述output
 * @param output_size TODO: 描述output_size
 * @return 0成功, -1失败
 */
int axk_tool_cron_remove_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    cJSON *job_id;

    if (!input_json || !output || output_size == 0) {
        return -1;
    }

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    job_id = cJSON_GetObjectItem(root, "job_id");
    if (!cJSON_IsString(job_id) || !job_id->valuestring) {
        snprintf(output, output_size, "Error: missing required field 'job_id'");
        cJSON_Delete(root);
        return -1;
    }

    if (axk_cron_remove_job(job_id->valuestring) != 0) {
        snprintf(output, output_size, "Error: cron job '%s' not found", job_id->valuestring);
        cJSON_Delete(root);
        return -1;
    }

    snprintf(output, output_size, "Cron job '%s' removed successfully.", job_id->valuestring);
    cJSON_Delete(root);
    return 0;
}
