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

/* @brief TODO: 描述axk_kv_set_blob的功能 @param key TODO: 描述key @param value TODO: 描述value @param len TODO: 描述len @return 0成功, -1失败 */
int axk_kv_set_blob(const char *key, const void *value, size_t len);
/* @brief TODO: 描述axk_kv_get_blob的功能 @param key TODO: 描述key @param buf TODO: 描述buf @param buf_len TODO: 描述buf_len @param saved_len TODO: 描述saved_len @return 0成功, -1失败 */
int axk_kv_get_blob(const char *key, void *buf, size_t buf_len, size_t *saved_len);
/* @brief TODO: 描述axk_kv_del的功能 @param key TODO: 描述key @return 0成功, -1失败 */
int axk_kv_del(const char *key);
/* @brief TODO: 描述axk_kv_exists的功能 @param key TODO: 描述key @return 0成功, -1失败 */
bool axk_kv_exists(const char *key);
/* @brief TODO: 描述axk_kv_clear的功能 @return 0成功, -1失败 */
int axk_kv_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_STORAGE_H */
