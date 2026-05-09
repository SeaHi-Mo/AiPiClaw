/**
 * @file axk_tool_files.c
 * @brief filemgrtool (LittleFS + FatFS) - 安信可科技 BL618 port
 * @note 整合自官方solution/mimiclaw/port
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_tool_files.h"

#include "axk_platform.h"
#include "axk_platform.h"
#include "mimi_config.h"

#include <cJSON.h>
#include <ff.h>
#include <fatfs_diskio_register.h>
#include <lfs.h>
#include <lfs_port.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tool_files_lfs";

#define MAX_FILE_SIZE (32 * 1024)
#define LFS_PATH_MAX_LEN 256
#define USER_PATH_MAX_LEN (LFS_PATH_MAX_LEN + 16)
#define FATFS_PATH_MAX_LEN 256

typedef enum {
    TOOL_FS_BACKEND_INVALID = 0,
    TOOL_FS_BACKEND_LFS,
    TOOL_FS_BACKEND_FATFS,
} tool_fs_backend_t;

static const char *s_lfs_partitions[] = {
    "media",
    "DATA",
    "PSM",
};

static struct lfs_context s_lfs_ctx = { .partition_name = "media" };
static struct lfs_config s_lfs_cfg = {
    .read_size = 256,
    .prog_size = 256,
    .lookahead_size = 256,
    .cache_size = 512,
    .block_size = 4096,
    .block_cycles = 500,
};
static lfs_t *s_lfs;

static FATFS s_fatfs;
static bool s_fatfs_driver_ready;
static bool s_fatfs_mounted;

/* Optional board hooks. When absent on a target, SD backend will fail gracefully. */
__attribute__((weak)) void board_sdh_gpio_init(void);
__attribute__((weak)) void fatfs_sdh_driver_register(void);

static int ensure_lfs_ready(void)
{
    size_t i;

    if (s_lfs) {
        return 0;
    }

    for (i = 0; i < sizeof(s_lfs_partitions) / sizeof(s_lfs_partitions[0]); i++) {
        s_lfs_ctx.partition_name = (char *)s_lfs_partitions[i];
        s_lfs = lfs_xip_init(&s_lfs_ctx, &s_lfs_cfg);
        if (s_lfs) {
            AXK_LOG_INFO("info", "LittleFS mounted on partition: %s", s_lfs_partitions[i]);
            return 0;
        }
    }

    AXK_LOG_ERROR("err", "Failed to initialize LittleFS on media/DATA/PSM");
    return -1;
}

int axk_tool_files_init(void)
{
    return (ensure_lfs_ready() == 0) ? 0 : -1;
}

struct lfs *axk_tool_files_get_lfs(void)
{
    if (ensure_lfs_ready() != 0) {
        return NULL;
    }
    return s_lfs;
}

static bool is_sd_path(const char *path)
{
    return path &&
           strncmp(path, "/sd", 3) == 0 &&
           (path[3] == '\0' || path[3] == '/');
}

static bool is_spiffs_path(const char *path)
{
    size_t base_len;

    if (!path) {
        return false;
    }

    base_len = strlen(MIMI_SPIFFS_BASE);
    if (strncmp(path, MIMI_SPIFFS_BASE, base_len) != 0) {
        return false;
    }

    return (path[base_len] == '\0' || path[base_len] == '/');
}

static tool_fs_backend_t detect_backend(const char *path)
{
    if (!path || strstr(path, "..")) {
        return TOOL_FS_BACKEND_INVALID;
    }

    if (is_spiffs_path(path)) {
        return TOOL_FS_BACKEND_LFS;
    }

    if (is_sd_path(path)) {
        return TOOL_FS_BACKEND_FATFS;
    }

    return TOOL_FS_BACKEND_INVALID;
}

static int ensure_fatfs_ready(void)
{
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
    FRESULT ret;

    if (s_fatfs_mounted) {
        return 0;
    }

    if (!s_fatfs_driver_ready) {
        if (board_sdh_gpio_init) {
            board_sdh_gpio_init();
        }

        if (fatfs_sdh_driver_register) {
            fatfs_sdh_driver_register();
        } else {
            AXK_LOG_ERROR("err", "fatfs_sdh_driver_register not available");
            return -1;
        }

        s_fatfs_driver_ready = true;
    }

    ret = f_mount(&s_fatfs, "/sd", 1);
    if (ret != FR_OK) {
        AXK_LOG_WARN("warn", "f_mount(/sd) failed: %d", ret);
        return -1;
    }

    s_fatfs_mounted = true;
    return 0;
#else
    AXK_LOG_WARN("warn", "FatFS SD backend not enabled by config");
    return -5;
#endif
}

static bool validate_user_path(const char *path)
{
    size_t base_len;

    if (!path) {
        return false;
    }

    if (strstr(path, "..") != NULL) {
        return false;
    }

    base_len = strlen(MIMI_SPIFFS_BASE);
    if (strncmp(path, MIMI_SPIFFS_BASE, base_len) != 0) {
        return false;
    }

    if (path[base_len] != '\0' && path[base_len] != '/') {
        return false;
    }

    return true;
}

static int user_path_to_lfs_path(const char *user_path, char *lfs_path, size_t lfs_path_size)
{
    const char *suffix;
    size_t base_len = strlen(MIMI_SPIFFS_BASE);

    if (!validate_user_path(user_path) || !lfs_path || lfs_path_size == 0) {
        return -1;
    }

    suffix = user_path + base_len;
    if (*suffix == '\0') {
        suffix = "/";
    }

    if (*suffix != '/') {
        return -1;
    }

    if (snprintf(lfs_path, lfs_path_size, "%s", suffix) >= (int)lfs_path_size) {
        return -1;
    }

    return 0;
}

static void lfs_path_to_user_path(const char *lfs_path, char *user_path, size_t user_path_size)
{
    if (!lfs_path || !user_path || user_path_size == 0) {
        return;
    }

    if (strcmp(lfs_path, "/") == 0) {
        snprintf(user_path, user_path_size, "%s", MIMI_SPIFFS_BASE);
        return;
    }

    if (lfs_path[0] == '/') {
        snprintf(user_path, user_path_size, "%s%s", MIMI_SPIFFS_BASE, lfs_path);
    } else {
        snprintf(user_path, user_path_size, "%s/%s", MIMI_SPIFFS_BASE, lfs_path);
    }
}

static int ensure_parent_dirs(lfs_t *lfs, const char *lfs_path)
{
    size_t i;
    size_t len;
    char tmp[LFS_PATH_MAX_LEN];

    len = strlen(lfs_path);
    if (len == 0 || len >= sizeof(tmp)) {
        return LFS_ERR_INVAL;
    }

    memcpy(tmp, lfs_path, len + 1);
    for (i = 1; i < len; i++) {
        int ret;
        if (tmp[i] != '/') {
            continue;
        }

        tmp[i] = '\0';
        ret = lfs_mkdir(lfs, tmp);
        tmp[i] = '/';
        if (ret != LFS_ERR_OK && ret != LFS_ERR_EXIST) {
            return ret;
        }
    }

    return LFS_ERR_OK;
}

static int append_line(char *output, size_t output_size, size_t *off, const char *line)
{
    int n;

    if (*off >= output_size) {
        return -1;
    }

    n = snprintf(output + *off, output_size - *off, "%s\n", line);
    if (n < 0 || (size_t)n >= (output_size - *off)) {
        *off = output_size;
        return -1;
    }

    *off += (size_t)n;
    return 0;
}

static void list_dir_recursive(lfs_t *lfs,
                               const char *dir_path,
                               const char *prefix,
                               char *output,
                               size_t output_size,
                               size_t *off,
                               int *count)
{
    lfs_dir_t dir;
    struct lfs_info info;
    int ret;

    ret = lfs_dir_open(lfs, &dir, dir_path);
    if (ret != LFS_ERR_OK) {
        return;
    }

    while ((ret = lfs_dir_read(lfs, &dir, &info)) > 0) {
        char lfs_child[LFS_PATH_MAX_LEN];
        char user_child[USER_PATH_MAX_LEN];

        if (strcmp(info.name, ".") == 0 || strcmp(info.name, "..") == 0) {
            continue;
        }

        if (strcmp(dir_path, "/") == 0) {
            snprintf(lfs_child, sizeof(lfs_child), "/%s", info.name);
        } else {
            snprintf(lfs_child, sizeof(lfs_child), "%s/%s", dir_path, info.name);
        }

        if (info.type == LFS_TYPE_DIR) {
            list_dir_recursive(lfs, lfs_child, prefix, output, output_size, off, count);
            continue;
        }

        if (info.type != LFS_TYPE_REG) {
            continue;
        }

        lfs_path_to_user_path(lfs_child, user_child, sizeof(user_child));
        if (prefix && strncmp(user_child, prefix, strlen(prefix)) != 0) {
            continue;
        }

        if (append_line(output, output_size, off, user_child) == 0) {
            (*count)++;
        } else {
            break;
        }
    }

    lfs_dir_close(lfs, &dir);
}

static int ensure_parent_dirs_fatfs(const char *path)
{
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
    char tmp[FATFS_PATH_MAX_LEN];
    size_t i;
    size_t len;

    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return -1;
    }

    memcpy(tmp, path, len + 1);
    for (i = 4; i < len; i++) {
        FILINFO info;
        FRESULT stat_ret;
        if (tmp[i] != '/') {
            continue;
        }

        tmp[i] = '\0';
        stat_ret = f_stat(tmp, &info);
        if (stat_ret != FR_OK) {
            FRESULT mk_ret = f_mkdir(tmp);
            if (mk_ret != FR_OK && mk_ret != FR_EXIST) {
                return -1;
            }
        }
        tmp[i] = '/';
    }

    return 0;
#else
    (void)path;
    return -5;
#endif
}

static void list_dir_recursive_fatfs(const char *dir_path,
                                     const char *prefix,
                                     char *output,
                                     size_t output_size,
                                     size_t *off,
                                     int *count)
{
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
    DIR dir;
    FILINFO info;
    FRESULT ret;

    ret = f_opendir(&dir, dir_path);
    if (ret != FR_OK) {
        return;
    }

    while (1) {
        char child[FATFS_PATH_MAX_LEN];

        ret = f_readdir(&dir, &info);
        if (ret != FR_OK || info.fname[0] == '\0') {
            break;
        }

        if (strcmp(info.fname, ".") == 0 || strcmp(info.fname, "..") == 0) {
            continue;
        }

        if (strcmp(dir_path, "/sd") == 0) {
            snprintf(child, sizeof(child), "/sd/%s", info.fname);
        } else {
            snprintf(child, sizeof(child), "%s/%s", dir_path, info.fname);
        }

        if (info.fattrib & AM_DIR) {
            list_dir_recursive_fatfs(child, prefix, output, output_size, off, count);
            continue;
        }

        if (prefix && strncmp(child, prefix, strlen(prefix)) != 0) {
            continue;
        }

        if (append_line(output, output_size, off, child) == 0) {
            (*count)++;
        } else {
            break;
        }
    }

    f_closedir(&dir);
#else
    (void)dir_path;
    (void)prefix;
    (void)output;
    (void)output_size;
    (void)off;
    (void)count;
#endif
}

int axk_tool_read_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    const char *path;
    tool_fs_backend_t backend;

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    backend = detect_backend(path);
    if (backend == TOOL_FS_BACKEND_INVALID) {
        snprintf(output, output_size, "Error: path must start with /sd or %s and must not contain '..'",
                 MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return -1;
    }

    if (backend == TOOL_FS_BACKEND_LFS) {
        char lfs_path[LFS_PATH_MAX_LEN];
        lfs_file_t file;
        int ret;
        int read_len;

        if (ensure_lfs_ready() != 0) {
            snprintf(output, output_size, "Error: LittleFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        if (user_path_to_lfs_path(path, lfs_path, sizeof(lfs_path)) != 0) {
            snprintf(output, output_size, "Error: invalid littlefs path");
            cJSON_Delete(root);
            return -1;
        }

        ret = lfs_file_open(s_lfs, &file, lfs_path, LFS_O_RDONLY);
        if (ret != LFS_ERR_OK) {
            snprintf(output, output_size, "Error: file not found: %s", path ? path : "(null)");
            cJSON_Delete(root);
            return -4;
        }

        read_len = lfs_file_read(s_lfs, &file, output, output_size > 0 ? output_size - 1 : 0);
        lfs_file_close(s_lfs, &file);
        cJSON_Delete(root);
        if (read_len < 0) {
            snprintf(output, output_size, "Error: failed to read file");
            return -1;
        }

        output[read_len] = '\0';
        return 0;
    }

    if (backend == TOOL_FS_BACKEND_FATFS) {
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
        FIL file;
        FRESULT fr;
        UINT read_len = 0;
        UINT max_read = (output_size > 0) ? (UINT)(output_size - 1) : 0;
        if (max_read > MAX_FILE_SIZE) {
            max_read = MAX_FILE_SIZE;
        }

        if (ensure_fatfs_ready() != 0) {
            snprintf(output, output_size, "Error: SD FatFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        fr = f_open(&file, path, FA_READ);
        if (fr != FR_OK) {
            snprintf(output, output_size, "Error: file not found: %s", path ? path : "(null)");
            cJSON_Delete(root);
            return -4;
        }

        fr = f_read(&file, output, max_read, &read_len);
        f_close(&file);
        cJSON_Delete(root);
        if (fr != FR_OK) {
            snprintf(output, output_size, "Error: failed to read file");
            return -1;
        }

        output[read_len] = '\0';
        return 0;
#else
        snprintf(output, output_size, "Error: FatFS SD backend not enabled");
        cJSON_Delete(root);
        return -5;
#endif
    }

    cJSON_Delete(root);
    snprintf(output, output_size, "Error: unsupported backend");
    return -1;
}

int axk_tool_write_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    const char *path;
    const char *content;
    tool_fs_backend_t backend;

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    content = cJSON_GetStringValue(cJSON_GetObjectItem(root, "content"));

    backend = detect_backend(path);
    if (backend == TOOL_FS_BACKEND_INVALID) {
        snprintf(output, output_size, "Error: path must start with /sd or %s and must not contain '..'",
                 MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return -1;
    }

    if (!content) {
        snprintf(output, output_size, "Error: missing 'content' field");
        cJSON_Delete(root);
        return -1;
    }

    if (backend == TOOL_FS_BACKEND_LFS) {
        char lfs_path[LFS_PATH_MAX_LEN];
        lfs_file_t file;
        int ret;
        size_t content_len;

        if (ensure_lfs_ready() != 0) {
            snprintf(output, output_size, "Error: LittleFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        if (user_path_to_lfs_path(path, lfs_path, sizeof(lfs_path)) != 0) {
            snprintf(output, output_size, "Error: invalid littlefs path");
            cJSON_Delete(root);
            return -1;
        }

        ret = ensure_parent_dirs(s_lfs, lfs_path);
        if (ret != LFS_ERR_OK) {
            snprintf(output, output_size, "Error: failed to create parent directories");
            cJSON_Delete(root);
            return -1;
        }

        ret = lfs_file_open(s_lfs, &file, lfs_path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (ret != LFS_ERR_OK) {
            snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
            cJSON_Delete(root);
            return -1;
        }

        content_len = strlen(content);
        ret = lfs_file_write(s_lfs, &file, content, content_len);
        lfs_file_close(s_lfs, &file);
        cJSON_Delete(root);
        if (ret < 0 || (size_t)ret != content_len) {
            snprintf(output, output_size, "Error: write failed for %s", path);
            return -1;
        }

        snprintf(output, output_size, "OK: wrote %d bytes to %s", ret, path);
        return 0;
    }

    if (backend == TOOL_FS_BACKEND_FATFS) {
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
        FIL file;
        FRESULT fr;
        UINT written = 0;
        size_t content_len = strlen(content);

        if (ensure_fatfs_ready() != 0) {
            snprintf(output, output_size, "Error: SD FatFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        if (ensure_parent_dirs_fatfs(path) != 0) {
            snprintf(output, output_size, "Error: failed to create parent directories");
            cJSON_Delete(root);
            return -1;
        }

        fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
            snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
            cJSON_Delete(root);
            return -1;
        }

        fr = f_write(&file, content, (UINT)content_len, &written);
        f_close(&file);
        cJSON_Delete(root);
        if (fr != FR_OK || written != (UINT)content_len) {
            snprintf(output, output_size, "Error: write failed for %s", path);
            return -1;
        }

        snprintf(output, output_size, "OK: wrote %u bytes to %s", (unsigned int)written, path);
        return 0;
#else
        snprintf(output, output_size, "Error: FatFS SD backend not enabled");
        cJSON_Delete(root);
        return -5;
#endif
    }

    cJSON_Delete(root);
    snprintf(output, output_size, "Error: unsupported backend");
    return -1;
}

int axk_tool_edit_file_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root;
    const char *path;
    const char *old_str;
    const char *new_str;
    tool_fs_backend_t backend;

    root = cJSON_Parse(input_json);
    if (!root) {
        snprintf(output, output_size, "Error: invalid JSON input");
        return -1;
    }

    path = cJSON_GetStringValue(cJSON_GetObjectItem(root, "path"));
    old_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "old_string"));
    new_str = cJSON_GetStringValue(cJSON_GetObjectItem(root, "new_string"));

    backend = detect_backend(path);
    if (backend == TOOL_FS_BACKEND_INVALID) {
        snprintf(output, output_size, "Error: path must start with /sd or %s and must not contain '..'",
                 MIMI_SPIFFS_BASE);
        cJSON_Delete(root);
        return -1;
    }

    if (!old_str || !new_str) {
        snprintf(output, output_size, "Error: missing 'old_string' or 'new_string' field");
        cJSON_Delete(root);
        return -1;
    }

    if (backend == TOOL_FS_BACKEND_LFS) {
        char lfs_path[LFS_PATH_MAX_LEN];
        lfs_file_t file;
        int ret;
        int file_size;
        char *buf = NULL;
        char *result = NULL;
        char *pos;
        size_t old_len;
        size_t new_len;
        size_t prefix_len;
        size_t suffix_start;
        size_t suffix_len;
        size_t total;
        size_t max_result;

        if (ensure_lfs_ready() != 0) {
            snprintf(output, output_size, "Error: LittleFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        if (user_path_to_lfs_path(path, lfs_path, sizeof(lfs_path)) != 0) {
            snprintf(output, output_size, "Error: invalid littlefs path");
            cJSON_Delete(root);
            return -1;
        }

        ret = lfs_file_open(s_lfs, &file, lfs_path, LFS_O_RDONLY);
        if (ret != LFS_ERR_OK) {
            snprintf(output, output_size, "Error: file not found: %s", path);
            cJSON_Delete(root);
            return -4;
        }

        file_size = lfs_file_size(s_lfs, &file);
        if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
            lfs_file_close(s_lfs, &file);
            snprintf(output, output_size, "Error: file too large or empty (%d bytes)", file_size);
            cJSON_Delete(root);
            return -1;
        }

        buf = malloc((size_t)file_size + 1);
        if (!buf) {
            lfs_file_close(s_lfs, &file);
            snprintf(output, output_size, "Error: out of memory");
            cJSON_Delete(root);
            return -1;
        }

        ret = lfs_file_read(s_lfs, &file, buf, (size_t)file_size);
        lfs_file_close(s_lfs, &file);
        if (ret < 0) {
            free(buf);
            snprintf(output, output_size, "Error: failed to read file");
            cJSON_Delete(root);
            return -1;
        }
        buf[ret] = '\0';

        pos = strstr(buf, old_str);
        if (!pos) {
            free(buf);
            snprintf(output, output_size, "Error: old_string not found in %s", path);
            cJSON_Delete(root);
            return -4;
        }

        old_len = strlen(old_str);
        new_len = strlen(new_str);
        max_result = (size_t)ret + ((new_len > old_len) ? (new_len - old_len) : 0) + 1;
        result = malloc(max_result);
        if (!result) {
            free(buf);
            snprintf(output, output_size, "Error: out of memory");
            cJSON_Delete(root);
            return -1;
        }

        prefix_len = (size_t)(pos - buf);
        memcpy(result, buf, prefix_len);
        memcpy(result + prefix_len, new_str, new_len);
        suffix_start = prefix_len + old_len;
        suffix_len = (size_t)ret - suffix_start;
        memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
        total = prefix_len + new_len + suffix_len;
        result[total] = '\0';
        free(buf);

        ret = lfs_file_open(s_lfs, &file, lfs_path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
        if (ret != LFS_ERR_OK) {
            free(result);
            snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
            cJSON_Delete(root);
            return -1;
        }

        ret = lfs_file_write(s_lfs, &file, result, total);
        lfs_file_close(s_lfs, &file);
        free(result);
        cJSON_Delete(root);
        if (ret < 0 || (size_t)ret != total) {
            snprintf(output, output_size, "Error: write failed for %s", path);
            return -1;
        }

        snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)",
                 path,
                 (int)old_len,
                 (int)new_len);
        return 0;
    }

    if (backend == TOOL_FS_BACKEND_FATFS) {
#if defined(CONFIG_FATFS) && defined(CONFIG_FATFS_SDH_SDCARD)
        FIL file;
        FRESULT fr;
        UINT io_len;
        FILINFO info;
        int file_size;
        char *buf = NULL;
        char *result = NULL;
        char *pos;
        size_t old_len;
        size_t new_len;
        size_t prefix_len;
        size_t suffix_start;
        size_t suffix_len;
        size_t total;
        size_t max_result;

        if (ensure_fatfs_ready() != 0) {
            snprintf(output, output_size, "Error: SD FatFS init failed");
            cJSON_Delete(root);
            return -1;
        }

        fr = f_stat(path, &info);
        if (fr != FR_OK) {
            snprintf(output, output_size, "Error: file not found: %s", path);
            cJSON_Delete(root);
            return -4;
        }
        file_size = (int)info.fsize;
        if (file_size <= 0 || file_size > MAX_FILE_SIZE) {
            snprintf(output, output_size, "Error: file too large or empty (%d bytes)", file_size);
            cJSON_Delete(root);
            return -1;
        }

        buf = malloc((size_t)file_size + 1);
        if (!buf) {
            snprintf(output, output_size, "Error: out of memory");
            cJSON_Delete(root);
            return -1;
        }

        fr = f_open(&file, path, FA_READ);
        if (fr != FR_OK) {
            free(buf);
            snprintf(output, output_size, "Error: file not found: %s", path);
            cJSON_Delete(root);
            return -4;
        }
        fr = f_read(&file, buf, (UINT)file_size, &io_len);
        f_close(&file);
        if (fr != FR_OK) {
            free(buf);
            snprintf(output, output_size, "Error: failed to read file");
            cJSON_Delete(root);
            return -1;
        }
        buf[io_len] = '\0';

        pos = strstr(buf, old_str);
        if (!pos) {
            free(buf);
            snprintf(output, output_size, "Error: old_string not found in %s", path);
            cJSON_Delete(root);
            return -4;
        }

        old_len = strlen(old_str);
        new_len = strlen(new_str);
        max_result = (size_t)io_len + ((new_len > old_len) ? (new_len - old_len) : 0) + 1;
        result = malloc(max_result);
        if (!result) {
            free(buf);
            snprintf(output, output_size, "Error: out of memory");
            cJSON_Delete(root);
            return -1;
        }

        prefix_len = (size_t)(pos - buf);
        memcpy(result, buf, prefix_len);
        memcpy(result + prefix_len, new_str, new_len);
        suffix_start = prefix_len + old_len;
        suffix_len = (size_t)io_len - suffix_start;
        memcpy(result + prefix_len + new_len, buf + suffix_start, suffix_len);
        total = prefix_len + new_len + suffix_len;
        result[total] = '\0';
        free(buf);

        fr = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (fr != FR_OK) {
            free(result);
            snprintf(output, output_size, "Error: cannot open file for writing: %s", path);
            cJSON_Delete(root);
            return -1;
        }
        fr = f_write(&file, result, (UINT)total, &io_len);
        f_close(&file);
        free(result);
        cJSON_Delete(root);
        if (fr != FR_OK || io_len != (UINT)total) {
            snprintf(output, output_size, "Error: write failed for %s", path);
            return -1;
        }

        snprintf(output, output_size, "OK: edited %s (replaced %d bytes with %d bytes)",
                 path,
                 (int)old_len,
                 (int)new_len);
        return 0;
#else
        snprintf(output, output_size, "Error: FatFS SD backend not enabled");
        cJSON_Delete(root);
        return -5;
#endif
    }

    cJSON_Delete(root);
    snprintf(output, output_size, "Error: unsupported backend");
    return -1;
}

int axk_tool_list_dir_execute(const char *input_json, char *output, size_t output_size)
{
    cJSON *root = NULL;
    const char *prefix = NULL;
    tool_fs_backend_t backend = TOOL_FS_BACKEND_INVALID;
    size_t off = 0;
    int count = 0;

    if (input_json) {
        root = cJSON_Parse(input_json);
        if (root) {
            cJSON *pfx = cJSON_GetObjectItem(root, "prefix");
            if (pfx && cJSON_IsString(pfx)) {
                prefix = pfx->valuestring;
                backend = detect_backend(prefix);
                if (backend == TOOL_FS_BACKEND_INVALID) {
                    snprintf(output, output_size, "Error: prefix must start with /sd or %s and must not contain '..'",
                             MIMI_SPIFFS_BASE);
                    cJSON_Delete(root);
                    return -1;
                }
            }
        }
    }

    if (!prefix) {
        backend = TOOL_FS_BACKEND_INVALID;
    }

    output[0] = '\0';

    if (backend == TOOL_FS_BACKEND_INVALID || backend == TOOL_FS_BACKEND_LFS) {
        if (ensure_lfs_ready() == 0) {
            list_dir_recursive(s_lfs, "/", prefix, output, output_size, &off, &count);
        } else if (backend == TOOL_FS_BACKEND_LFS) {
            snprintf(output, output_size, "Error: LittleFS init failed");
            cJSON_Delete(root);
            return -1;
        }
    }

    if (backend == TOOL_FS_BACKEND_INVALID || backend == TOOL_FS_BACKEND_FATFS) {
        if (ensure_fatfs_ready() == 0) {
            list_dir_recursive_fatfs("/sd", prefix, output, output_size, &off, &count);
        } else if (backend == TOOL_FS_BACKEND_FATFS) {
            snprintf(output, output_size, "Error: SD FatFS init failed");
            cJSON_Delete(root);
            return -1;
        }
    }

    if (count == 0) {
        snprintf(output, output_size, "(no files found)");
    }

    cJSON_Delete(root);
    return 0;
}
