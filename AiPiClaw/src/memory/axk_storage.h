/**
 * @file axk_storage.h
 * @brief KVstoremodule头file
 * @copyright Copyright (c) 2026 AI-Thinker
 */
#ifndef __AXK_STORAGE_H
#define __AXK_STORAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int axk_kv_set_blob(const char *key, const void *value, size_t len);
int axk_kv_get_blob(const char *key, void *buf, size_t buf_len, size_t *saved_len);
int axk_kv_del(const char *key);
bool axk_kv_exists(const char *key);
int axk_kv_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_STORAGE_H */
