#include "tools/tool_cron.h"
#include "cron/cron_service.h"
#include "axk_message_bus.h"

#include <string.h>
#include <time.h>
#include "time_compat.h"  /**< BL618 time() 兼容层 */
#include "axk_platform.h"
#include "cJSON.h"

static const char *TAG = "tool_cron";

/* ── cron_add ─────────────────────────────────────────────────── */

int axk_tool_cron_add_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
    const char *schedule_type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "schedule_type"));
    const char *message = cJSON_GetStringValue(cJSON_GetObjectItem(root, "message"));

    if (!name || !schedule_type || !message) {
        snprintf(output, output_size, "Error: missing required fields (name, schedule_type, message)");
        cJSON_Delete(root);
        return -1;
    }

    if (strlen(message) == 0) {
        snprintf(output, output_size, "Error: message must not be empty");
        cJSON_Delete(root);
        return -1;
    }

    cron_job_t job;
    memset(&job, 0, sizeof(job));
    strncpy(job.name, name, sizeof(job.name) - 1);
    strncpy(job.message, message, sizeof(job.message) - 1);

    /* Optional channel and chat_id */
    const char *channel = cJSON_GetStringValue(cJSON_GetObjectItem(root, "channel"));
    const char *chat_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "chat_id"));
    if (channel) strncpy(job.channel, channel, sizeof(job.channel) - 1);
    if (chat_id) strncpy(job.chat_id, chat_id, sizeof(job.chat_id) - 1);

    if (strcmp(job.channel, MIMI_CHAN_TELEGRAM) == 0 &&
        (job.chat_id[0] == '\0' || strcmp(job.chat_id, "cron") == 0)) {
        snprintf(output, output_size,
                 "Error: cron_add with channel='telegram' requires a valid chat_id");
        cJSON_Delete(root);
        return -1;
    }

    if (strcmp(schedule_type, "every") == 0) {
        job.kind = CRON_KIND_EVERY;
        cJSON *interval = cJSON_GetObjectItem(root, "interval_s");
        if (!interval || !cJSON_IsNumber(interval) || interval->valuedouble <= 0) {
            snprintf(output, output_size, "Error: 'every' schedule requires positive 'interval_s'");
            cJSON_Delete(root);
            return -1;
        }
        job.interval_s = (uint32_t)interval->valuedouble;
        job.delete_after_run = false;
    } else if (strcmp(schedule_type, "at") == 0) {
        job.kind = CRON_KIND_AT;
        cJSON *at_epoch = cJSON_GetObjectItem(root, "at_epoch");
        if (!at_epoch || !cJSON_IsNumber(at_epoch)) {
            snprintf(output, output_size, "Error: 'at' schedule requires 'at_epoch' (unix timestamp)");
            cJSON_Delete(root);
            return -1;
        }
        job.at_epoch = (int64_t)at_epoch->valuedouble;

        /* Check if already in the past */
        time_t now = time(NULL);
        if (job.at_epoch <= now) {
            snprintf(output, output_size, "Error: at_epoch %lld is in the past (now=%lld)",
                     (long long)job.at_epoch, (long long)now);
            cJSON_Delete(root);
            return -1;
        }

        /* Default: delete one-shot jobs after run */
        cJSON *delete_j = cJSON_GetObjectItem(root, "delete_after_run");
        job.delete_after_run = delete_j ? cJSON_IsTrue(delete_j) : true;
    } else {
        snprintf(output, output_size, "Error: schedule_type must be 'every' or 'at'");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);

    int err = axk_cron_add_job(&job);
    if (err != 0) {
        snprintf(output, output_size, "Error: failed to add job (%d)", err);
        return err;
    }

    /* Format success response */
    if (job.kind == CRON_KIND_EVERY) {
        snprintf(output, output_size,
                 "OK: Added recurring job '%s' (id=%s), runs every %lu seconds. Next run at epoch %lld.",
                 job.name, job.id, (unsigned long)job.interval_s, (long long)job.next_run);
    } else {
        snprintf(output, output_size,
                 "OK: Added one-shot job '%s' (id=%s), fires at epoch %lld.%s",
                 job.name, job.id, (long long)job.at_epoch,
                 job.delete_after_run ? " Will be deleted after firing." : "");
    }

    AXK_LOG_INFO("cron", "cron_add: %s", output);
    return 0;
}

/* ── cron_list ────────────────────────────────────────────────── */

int axk_tool_cron_list_execute(const char *input_json, char *output, size_t output_size)
{
    (void)input_json;

    const cron_job_t *jobs;
    int count;
    axk_cron_list_jobs(&jobs, &count);

    if (count == 0) {
        snprintf(output, output_size, "No cron jobs scheduled.");
        return 0;
    }

    size_t off = 0;
    off += snprintf(output + off, output_size - off,
                    "Scheduled jobs (%d):\n", count);

    for (int i = 0; i < count && off < output_size - 1; i++) {
        const cron_job_t *j = &jobs[i];

        if (j->kind == CRON_KIND_EVERY) {
            off += snprintf(output + off, output_size - off,
                "  %d. [%s] \"%s\" — every %lus, %s, next=%lld, last=%lld, ch=%s:%s\n",
                i + 1, j->id, j->name,
                (unsigned long)j->interval_s,
                j->enabled ? "enabled" : "disabled",
                (long long)j->next_run, (long long)j->last_run,
                j->channel, j->chat_id);
        } else {
            off += snprintf(output + off, output_size - off,
                "  %d. [%s] \"%s\" — at %lld, %s, last=%lld, ch=%s:%s%s\n",
                i + 1, j->id, j->name,
                (long long)j->at_epoch,
                j->enabled ? "enabled" : "disabled",
                (long long)j->last_run,
                j->channel, j->chat_id,
                j->delete_after_run ? " (auto-delete)" : "");
        }
    }

    AXK_LOG_INFO("cron", "cron_list: %d jobs", count);
    return 0;
}

/* ── cron_remove ──────────────────────────────────────────────── */

int axk_tool_cron_remove_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    const char *job_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "job_id"));
    if (!job_id || strlen(job_id) == 0) {
        snprintf(output, output_size, "Error: missing 'job_id' field");
        cJSON_Delete(root);
        return -1;
    }

    char job_id_copy[16] = {0};
    strncpy(job_id_copy, job_id, sizeof(job_id_copy) - 1);

    int err = axk_cron_remove_job(job_id_copy);
    cJSON_Delete(root);

    if (err == 0) {
        snprintf(output, output_size, "OK: Removed cron job %s", job_id_copy);
    } else if (err == -4) {
        snprintf(output, output_size, "Error: job '%s' not found", job_id_copy);
    } else {
        snprintf(output, output_size, "Error: failed to remove job (%d)", err);
    }

    AXK_LOG_INFO("cron", "cron_remove: %s -> %d", job_id_copy, err);
    return err;
}
