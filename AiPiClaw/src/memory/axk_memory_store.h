/**
 * @file axk_memory_store.h
 * @brief 堆内存键value store
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_MEMORY_STORE_H
#define __AXK_MEMORY_STORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int  axk_memory_store_init(void);
int  axk_memory_store_set_string(const char *key, const char *value);
int  axk_memory_store_get_string(const char *key, char *buf, size_t buf_size);
int  axk_memory_store_del(const char *key);
bool axk_memory_store_exists(const char *key);
void axk_memory_store_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MEMORY_STORE_H */
