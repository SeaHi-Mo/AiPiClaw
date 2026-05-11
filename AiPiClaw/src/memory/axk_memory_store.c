/**
 * @file axk_memory_store.c
 * @brief 堆内存键value store - 会话上下文缓存
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 基于链式hash 表轻量级 KV store，for 会话上下文 etc场景
 *       support  set / get / del / exists 操作
 */

#include "axk_memory_store.h"
#include "axk_platform.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define STORE_BUCKETS    32      /**< hash 桶count  */
#define KEY_MAX_LEN      64      /**< 键最大length  */
#define VAL_MAX_LEN      512     /**< value 最大length  */

typedef struct store_entry {
    char key[KEY_MAX_LEN];
    char value[VAL_MAX_LEN];
    struct store_entry *next;    /**< 链表解决hash 冲突 */
} store_entry_t;

static store_entry_t *s_buckets[STORE_BUCKETS];
static bool s_initialized = false;

/* ── hash func  ──────────────────────────────────── */

/* @brief TODO: 描述store_hash的功能 @param key TODO: 描述key @return 0成功, -1失败 */
static unsigned int store_hash(const char *key)
{
    unsigned int h = 5381;
    while (*key) {
        h = ((h << 5) + h) + (unsigned char)*key++;
    }
    return h % STORE_BUCKETS;
}

/* ── public  API ────────────────────────────────────── */

/* @brief TODO: 描述axk_memory_store_init的功能 @return 0成功, -1失败 */
int axk_memory_store_init(void)
{
    if (s_initialized) return 0;
    memset(s_buckets, 0, sizeof(s_buckets));
    s_initialized = true;
    AXK_LOG_INFO("[axk_memory_store] initok, %d桶\r\n", STORE_BUCKETS);
    return 0;
}

/* @brief TODO: 描述axk_memory_store_set_string的功能 @param key TODO: 描述key @param value TODO: 描述value @return 0成功, -1失败 */
int axk_memory_store_set_string(const char *key, const char *value)
{
    unsigned int bucket;
    store_entry_t *e;

    if (!key || !value || !s_initialized) return -1;

    bucket = store_hash(key);

    /* find 存in 键 */
    for (e = s_buckets[bucket]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            strncpy(e->value, value, VAL_MAX_LEN - 1);
            e->value[VAL_MAX_LEN - 1] = '\0';
            return 0;
        }
    }

    /* 新键 */
    e = (store_entry_t *)calloc(1, sizeof(store_entry_t));
    if (!e) {
        AXK_LOG_ERROR("[axk_memory_store] OOM\r\n");
        return -1;
    }

    strncpy(e->key, key, KEY_MAX_LEN - 1);
    strncpy(e->value, value, VAL_MAX_LEN - 1);
    e->next = s_buckets[bucket];
    s_buckets[bucket] = e;
    return 0;
}

/* @brief TODO: 描述axk_memory_store_get_string的功能 @param key TODO: 描述key @param buf TODO: 描述buf @param buf_size TODO: 描述buf_size @return 0成功, -1失败 */
int axk_memory_store_get_string(const char *key, char *buf, size_t buf_size)
{
    unsigned int bucket;
    store_entry_t *e;

    if (!key || !buf || buf_size == 0 || !s_initialized) return -1;

    bucket = store_hash(key);
    for (e = s_buckets[bucket]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) {
            strncpy(buf, e->value, buf_size - 1);
            buf[buf_size - 1] = '\0';
            return 0;
        }
    }
    return -1;
}

/* @brief TODO: 描述axk_memory_store_del的功能 @param key TODO: 描述key @return 0成功, -1失败 */
int axk_memory_store_del(const char *key)
{
    unsigned int bucket;
    store_entry_t *e, *prev = NULL;

    if (!key || !s_initialized) return -1;

    bucket = store_hash(key);
    for (e = s_buckets[bucket]; e; prev = e, e = e->next) {
        if (strcmp(e->key, key) == 0) {
            if (prev) prev->next = e->next;
            else      s_buckets[bucket] = e->next;
            free(e);
            return 0;
        }
    }
    return -1;
}

/* @brief TODO: 描述axk_memory_store_exists的功能 @param key TODO: 描述key @return 0成功, -1失败 */
bool axk_memory_store_exists(const char *key)
{
    unsigned int bucket;
    store_entry_t *e;

    if (!key || !s_initialized) return false;

    bucket = store_hash(key);
    for (e = s_buckets[bucket]; e; e = e->next) {
        if (strcmp(e->key, key) == 0) return true;
    }
    return false;
}

/* @brief TODO: 描述axk_memory_store_clear的功能 @return 无返回值 */
void axk_memory_store_clear(void)
{
    int i;

    if (!s_initialized) return;

    for (i = 0; i < STORE_BUCKETS; i++) {
        store_entry_t *e = s_buckets[i];
        while (e) {
            store_entry_t *next = e->next;
            free(e);
            e = next;
        }
        s_buckets[i] = NULL;
    }
    AXK_LOG_INFO("[axk_memory_store] 清empty \r\n");
}
