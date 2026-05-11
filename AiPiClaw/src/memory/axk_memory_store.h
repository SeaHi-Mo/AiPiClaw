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

/**
 * @brief TODO: 描述axk_memory_store_init的功能
 *
 * @return 0成功, -1失败
 */
int  axk_memory_store_init(void);
/**
 * @brief TODO: 描述axk_memory_store_set_string的功能
 *
 * @param key TODO: 描述key
 * @param value TODO: 描述value
 * @return 0成功, -1失败
 */
int  axk_memory_store_set_string(const char *key, const char *value);
/**
 * @brief TODO: 描述axk_memory_store_get_string的功能
 *
 * @param key TODO: 描述key
 * @param buf TODO: 描述buf
 * @param buf_size TODO: 描述buf_size
 * @return 0成功, -1失败
 */
int  axk_memory_store_get_string(const char *key, char *buf, size_t buf_size);
/**
 * @brief TODO: 描述axk_memory_store_del的功能
 *
 * @param key TODO: 描述key
 * @return 0成功, -1失败
 */
int  axk_memory_store_del(const char *key);
/**
 * @brief TODO: 描述axk_memory_store_exists的功能
 *
 * @param key TODO: 描述key
 * @return 0成功, -1失败
 */
bool axk_memory_store_exists(const char *key);
/**
 * @brief TODO: 描述axk_memory_store_clear的功能
 *
 * @return 无返回值
 */
void axk_memory_store_clear(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_MEMORY_STORE_H */
