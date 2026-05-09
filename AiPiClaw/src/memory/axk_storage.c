/**
 * @file axk_storage.c
 * @brief KVstoremodule - 基于 easyflash blob API
 * @version 1.0
 * @date 2026-04-29
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 整合自官方 solution/mimiclaw/port/src/mimiclaw_storage.c
 *       use  ef_get_env_blob / ef_set_env_blob (非废弃API)
 */

#include "axk_storage.h"
#include "axk_platform.h"

#include <easyflash.h>
#include <string.h>

/**
 * @brief set 键value 对（blobformat ）
 * @param[in] key 键名
 * @param[in] value value dataptr 
 * @param[in] len datalength 
 * @return OKreturn 0，FAILreturn -1
 */
int axk_kv_set_blob(const char *key, const void *value, size_t len)
{
    if (!key || !value || len == 0) {
        return -1;
    }

    if (ef_set_env_blob(key, value, len) != EF_NO_ERR) {
        return -1;
    }

    return (ef_save_env() == EF_NO_ERR) ? 0 : -1;
}

/**
 * @brief get 键value 对（blobformat ）
 * @param[in] key 键名
 * @param[out] buf output buffer 
 * @param[in] buf_len buffer size
 * @param[out] saved_len 实际datalength （可为NULL）
 * @return OKreturn 0，FAILreturn -1
 */
int axk_kv_get_blob(const char *key, void *buf, size_t buf_len, size_t *saved_len)
{
    size_t n;

    if (!key || !buf || buf_len == 0) {
        return -1;
    }

    n = ef_get_env_blob(key, buf, buf_len, saved_len);
    return (n > 0) ? 0 : -1;
}

/**
 * @brief delete 键value 对
 * @param[in] key 键名
 * @return OKreturn 0
 */
int axk_kv_del(const char *key)
{
    if (!key) return -1;
    ef_del_and_save_env(key);
    return 0;
}

/**
 * @brief check 键is否存in 
 * @param[in] key 键名
 * @return 存in return true
 */
bool axk_kv_exists(const char *key)
{
    size_t dummy;
    char c;
    if (!key) return false;
    return ef_get_env_blob(key, &c, 1, &dummy) > 0;
}

/**
 * @brief 清除all键value 对（遍历delete ）
 * @return OKreturn 0
 */
int axk_kv_clear(void)
{
    /**< easyflash 无全局清除API，call ef_env_set_defaultrestore default  */
    ef_env_set_default();
    ef_save_env();
    return 0;
}
