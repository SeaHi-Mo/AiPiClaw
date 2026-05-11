/**
 * @file axk_storage.h
 * @brief KVstore接口 - 替代ESP-IDFNVS
 * @version 1.0
 * @date 2026-04-20
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note use Bouffalo SDKeasyflash实现键value 对store
 */

#ifndef __AXK_STORAGE_H
#define __AXK_STORAGE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief set KVdata
 *
 * @param[in] key 键名
 * @param[in] value dataptr 
 * @param[in] len datalength 
 * @return OKreturn 0，FAILreturn -1
 */
int axk_kv_set_blob(const char *key, const void *value, size_t len);

/**
 * @brief get KVdata
 *
 * @param[in] key 键名
 * @param[out] buf buffer ptr 
 * @param[in] buf_len buffer size
 * @param[out] saved_len 实际save length （可为NULL）
 * @return OKreturn 0，FAILreturn -1
 */
int axk_kv_get_blob(const char *key, void *buf, size_t buf_len, size_t *saved_len);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_STORAGE_H */
