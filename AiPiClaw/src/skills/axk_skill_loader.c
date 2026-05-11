/**
 * @file axk_skill_loader.c
 * @brief skill loader实现 - mgrbuiltin 技能，support 动态load /spiffs/skills/ directory 下 .md 技能file
 * @version 1.0
 * @date 2026-04-28
 * @copyright Copyright (c) 2026 AI-Thinker
 */

#include "axk_skill_loader.h"
#include "axk_platform.h"
#include "axk_tool_get_time.h"
#include "axk_tool_gpio.h"
#include "axk_tool_files.h"
#include "axk_wifi_onboard.h"
#include "axk_wifi_manager.h"
#include "mimi_config.h"

#include <stdio.h>
#include <string.h>
#include <lfs.h>
#include <lfs_port.h>

#define AXK_MAX_SKILLS        12
#define AXK_SKILL_NAME_MAX    32
#define AXK_SKILL_DESC_MAX    64
#define AXK_SKILL_TRIG_MAX    32
#define AXK_SKILL_PATH_MAX    128
#define AXK_SKILL_BODY_MAX    512

/**
 * @brief 技能执行func type 
 */
typedef int (*axk_skill_exec_fn_t)(const char *args, char *out_buf, size_t out_size);

typedef struct {
    char name[AXK_SKILL_NAME_MAX];        /**< 技能name */
    char description[AXK_SKILL_DESC_MAX]; /**< 技能description */
    char trigger[AXK_SKILL_TRIG_MAX];     /**< trigger 关键词 */
    axk_skill_exec_fn_t exec;             /**< 执行func （builtin 技能） */
    bool registered;                      /**< is否registered */
    bool is_external;                     /**< is否为external .md fileload技能 */
    char filepath[AXK_SKILL_PATH_MAX];    /**< external skillsfilepath */
} axk_skill_t;

static axk_skill_t s_skills[AXK_MAX_SKILLS];
static int s_skill_count = 0;

/**
 * @brief register built-in skill
 *
 * @param[in] name 技能name 
 * @param[in] description 技能description 
 * @param[in] trigger trigger 关键词
 * @param[in] exec 执行func 
 * @return OKreturn 0，FAILreturn 非零
 */
static int axk_skill_register(const char *name, const char *description,
                               const char *trigger, axk_skill_exec_fn_t exec)
{
    if (s_skill_count >= AXK_MAX_SKILLS) {
        return -1;
    }

    strncpy(s_skills[s_skill_count].name, name, AXK_SKILL_NAME_MAX - 1);
    s_skills[s_skill_count].name[AXK_SKILL_NAME_MAX - 1] = '\0';

    strncpy(s_skills[s_skill_count].description, description, AXK_SKILL_DESC_MAX - 1);
    s_skills[s_skill_count].description[AXK_SKILL_DESC_MAX - 1] = '\0';

    strncpy(s_skills[s_skill_count].trigger, trigger, AXK_SKILL_TRIG_MAX - 1);
    s_skills[s_skill_count].trigger[AXK_SKILL_TRIG_MAX - 1] = '\0';

    s_skills[s_skill_count].exec = exec;
    s_skills[s_skill_count].registered = true;
    s_skills[s_skill_count].is_external = false;
    s_skills[s_skill_count].filepath[0] = '\0';
    s_skill_count++;

    AXK_LOG_INFO("[axk_skill_loader] register built-in skill: %s (trigger: %s)\r\n", name, trigger);
    return 0;
}

/**
 * @brief registerexternal skills（from  .md fileload）
 *
 * @param[in] name 技能name 
 * @param[in] description 技能description 
 * @param[in] trigger trigger 关键词
 * @param[in] filepath filepath 
 * @return OKreturn 0，FAILreturn 非零
 */
static int axk_skill_register_external(const char *name, const char *description,
                                        const char *trigger, const char *filepath)
{
    if (s_skill_count >= AXK_MAX_SKILLS) {
        return -1;
    }

    strncpy(s_skills[s_skill_count].name, name, AXK_SKILL_NAME_MAX - 1);
    s_skills[s_skill_count].name[AXK_SKILL_NAME_MAX - 1] = '\0';

    strncpy(s_skills[s_skill_count].description, description, AXK_SKILL_DESC_MAX - 1);
    s_skills[s_skill_count].description[AXK_SKILL_DESC_MAX - 1] = '\0';

    strncpy(s_skills[s_skill_count].trigger, trigger, AXK_SKILL_TRIG_MAX - 1);
    s_skills[s_skill_count].trigger[AXK_SKILL_TRIG_MAX - 1] = '\0';

    s_skills[s_skill_count].exec = NULL;
    s_skills[s_skill_count].registered = true;
    s_skills[s_skill_count].is_external = true;

    strncpy(s_skills[s_skill_count].filepath, filepath, AXK_SKILL_PATH_MAX - 1);
    s_skills[s_skill_count].filepath[AXK_SKILL_PATH_MAX - 1] = '\0';
    s_skill_count++;

    AXK_LOG_INFO("[axk_skill_loader] registerexternal skills: %s (trigger: %s, path : %s)\r\n",
                 name, trigger, filepath);
    return 0;
}

/**
 * @brief time 查询技能执行func 
 *
 * @param[in] args param 
 * @param[out] out_buf output buffer 
 * @param[in] out_size buffer size
 * @return OKreturn 0
 */
static int axk_skill_exec_time(const char *args, char *out_buf, size_t out_size)
{
    (void)args;
    if (out_buf && out_size > 0) {
        axk_tool_get_time_execute(NULL, out_buf, out_size);
    }
    return 0;
}

/**
 * @brief GPIO ctrl技能执行func 
 *
 * @param[in] args param （eg. "set 10 1"）
 * @param[out] out_buf output buffer 
 * @param[in] out_size buffer size
 * @return OKreturn 0
 */
static int axk_skill_exec_gpio(const char *args, char *out_buf, size_t out_size)
{
    (void)args;
    if (out_buf && out_size > 0) {
        snprintf(out_buf, out_size, "GPIO操作执行: %s", args ? args : "(no args)");
    }
    return 0;
}

/**
 * @brief WiFimgr技能执行func 
 *
 * @param[in] args param （format : "SSID PASSWORD"  or  "SSID"）
 * @param[out] out_buf output buffer 
 * @param[in] out_size buffer size
 * @return OKreturn 0
 */
static int axk_skill_exec_wifi(const char *args, char *out_buf, size_t out_size)
{
    char ssid[64] = {0};
    char pwd[64] = {0};

    if (!out_buf || out_size == 0) {
        return -1;
    }

    /* parse param : "SSID [PASSWORD]" */
    if (args && args[0] != '\0') {
        const char *space = strchr(args, ' ');
        if (space) {
            size_t ssid_len = (size_t)(space - args);
            if (ssid_len >= sizeof(ssid)) ssid_len = sizeof(ssid) - 1;
            strncpy(ssid, args, ssid_len);
            ssid[ssid_len] = '\0';

            /* skip empty 格 */
            const char *pwd_start = space + 1;
            while (*pwd_start == ' ') pwd_start++;
            strncpy(pwd, pwd_start, sizeof(pwd) - 1);
            pwd[sizeof(pwd) - 1] = '\0';
        } else {
            /* 只有SSID, 无password  */
            strncpy(ssid, args, sizeof(ssid) - 1);
            ssid[sizeof(ssid) - 1] = '\0';
        }
    } else {
        snprintf(out_buf, out_size, "usage: : wifi_set <SSID> [password ]\r\n"
                 "示eg.: wifi_set MyWiFi 12345678\r\n"
                 "      wifi_set OpenWiFi");
        return 0;
    }

    if (ssid[0] == '\0') {
        snprintf(out_buf, out_size, "ERROR: SSIDnot 能为empty ");
        return -1;
    }

    /* save WiFicredential  */
    if (axk_wifi_save_credentials(ssid, pwd[0] ? pwd : NULL) != 0) {
        snprintf(out_buf, out_size, "ERROR: save WiFicredential FAIL");
        return -1;
    }

    /* connectWiFi */
    int ret = axk_wifi_connect(ssid, pwd[0] ? pwd : NULL);
    if (ret != 0) {
        snprintf(out_buf, out_size, "WiFicredential save , but connectrequestsendFAIL: %d", ret);
        return -1;
    }

    snprintf(out_buf, out_size, "WiFicredential save , 正in connect %s ...", ssid);
    return 0;
}

/**
 * @brief parse 技能file前置元data（frontmatter）
 *
 * @note support 极简format ：
 *       ---
 *       name: skill_name
 *       trigger: trigger
 *       description: 技能description 
 *       ---
 *       body content 
 * @param[in] content filecontent 
 * @param[out] name parse 出name 
 * @param[in] name_size name buffer size
 * @param[out] trigger parse 出trigger
 * @param[in] trigger_size triggerbuffer size
 * @param[out] description parse 出description 
 * @param[in] desc_size description buffer size
 * @return OKreturn 0，FAILreturn -1
 */
static int axk_skill_parse_frontmatter(const char *content,
                                        char *name, size_t name_size,
                                        char *trigger, size_t trigger_size,
                                        char *description, size_t desc_size)
{
    const char *p = content;
    bool in_frontmatter = false;

    if (!content || !name || !trigger || !description) {
        return -1;
    }

    name[0] = '\0';
    trigger[0] = '\0';
    description[0] = '\0';

    /* check is否以 --- 开头 */
    if (strncmp(p, "---", 3) != 0) {
        /* 无 frontmatter，attempt 直接parse #一行为trigger，#二行为description  */
        const char *nl = strchr(p, '\n');
        if (nl) {
            size_t len = (size_t)(nl - p);
            if (len > 0 && len < trigger_size) {
                strncpy(trigger, p, len);
                trigger[len] = '\0';
                /* 去除换行符 */
                if (trigger[len - 1] == '\r') {
                    trigger[len - 1] = '\0';
                }
            }
            p = nl + 1;
            nl = strchr(p, '\n');
            if (nl) {
                len = (size_t)(nl - p);
                if (len > 0 && len < desc_size) {
                    strncpy(description, p, len);
                    description[len] = '\0';
                    if (description[len - 1] == '\r') {
                        description[len - 1] = '\0';
                    }
                }
            }
        }
        /* default use trigger作为 name */
        strncpy(name, trigger, name_size - 1);
        name[name_size - 1] = '\0';
        return (trigger[0] != '\0') ? 0 : -1;
    }

    /* skip #一行 --- */
    p += 3;
    while (*p == '\r' || *p == '\n') {
        p++;
    }
    in_frontmatter = true;

    while (*p != '\0' && in_frontmatter) {
        const char *line_end = strchr(p, '\n');
        char line[128];
        size_t line_len;

        if (!line_end) {
            line_end = p + strlen(p);
        }

        line_len = (size_t)(line_end - p);
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        strncpy(line, p, line_len);
        line[line_len] = '\0';
        if (line_len > 0 && line[line_len - 1] == '\r') {
            line[line_len - 1] = '\0';
        }

        /* check is否为 frontmatter end 标记 */
        if (strcmp(line, "---") == 0) {
            in_frontmatter = false;
            break;
        }

        /* parse  key: value */
        {
            char *colon = strchr(line, ':');
            if (colon) {
                *colon = '\0';
                char *key = line;
                char *value = colon + 1;

                /* 去除前导empty 格 */
                while (*value == ' ' || *value == '\t') {
                    value++;
                }

                if (strcmp(key, "name") == 0) {
                    strncpy(name, value, name_size - 1);
                    name[name_size - 1] = '\0';
                } else if (strcmp(key, "trigger") == 0) {
                    strncpy(trigger, value, trigger_size - 1);
                    trigger[trigger_size - 1] = '\0';
                } else if (strcmp(key, "description") == 0) {
                    strncpy(description, value, desc_size - 1);
                    description[desc_size - 1] = '\0';
                }
            }
        }

        p = line_end;
        if (*p == '\n') {
            p++;
        }
    }

    if (name[0] == '\0' && trigger[0] != '\0') {
        strncpy(name, trigger, name_size - 1);
        name[name_size - 1] = '\0';
    }

    return (trigger[0] != '\0') ? 0 : -1;
}

/**
 * @brief from  .md 技能file提取 body content （frontmatter after part ）
 *
 * @param[in] content filefull content 
 * @param[out] body output  body buffer 
 * @param[in] body_size buffer size
 * @return OKreturn 0，FAILreturn -1
 */
static int axk_skill_extract_body(const char *content, char *body, size_t body_size)
{
    const char *p = content;

    if (!content || !body || body_size == 0) {
        return -1;
    }

    body[0] = '\0';

    /* 果有 frontmatter，skip 它 */
    if (strncmp(p, "---", 3) == 0) {
        p += 3;
        while (*p != '\0') {
            if (strncmp(p, "---", 3) == 0) {
                p += 3;
                while (*p == '\r' || *p == '\n') {
                    p++;
                }
                break;
            }
            p++;
        }
    } else {
        /* 无 frontmatter，skip 前两行（trigger & description ） */
        const char *nl = strchr(p, '\n');
        if (nl) {
            p = nl + 1;
            nl = strchr(p, '\n');
            if (nl) {
                p = nl + 1;
            }
        }
    }

    /* copy 剩余content  */
    if (*p != '\0') {
        size_t len = strlen(p);
        if (len >= body_size) {
            len = body_size - 1;
        }
        strncpy(body, p, len);
        body[len] = '\0';
    }

    return 0;
}

/**
 * @brief from filesystemload /spiffs/skills/ directory 下 .md 技能file
 *
 * @return OKload技能count ，FAIL or 无filereturn 0
 */
int axk_skill_load_from_fs(void)
{
    lfs_t *lfs = axk_tool_files_get_lfs();
    lfs_dir_t dir;
    struct lfs_info info;
    int ret;
    int loaded = 0;
    char dir_path[64];

    snprintf(dir_path, sizeof(dir_path), "%s/skills", MIMI_SPIFFS_BASE);

    if (!lfs) {
        AXK_LOG_WARN("[axk_skill_loader] LittleFS not mounted，cannot loadexternal skills\r\n");
        return 0;
    }

    ret = lfs_dir_open(lfs, &dir, dir_path);
    if (ret < 0) {
        AXK_LOG_INFO("[axk_skill_loader] 技能directory  %s not 存in  or cannot open \r\n", dir_path);
        return 0;
    }

    while ((ret = lfs_dir_read(lfs, &dir, &info)) > 0) {
        char filepath[AXK_SKILL_PATH_MAX];
        lfs_file_t file;
        char filebuf[AXK_SKILL_BODY_MAX + 256];
        lfs_ssize_t read_len;
        char name[AXK_SKILL_NAME_MAX];
        char trigger[AXK_SKILL_TRIG_MAX];
        char description[AXK_SKILL_DESC_MAX];

        /* skip directory  & 非 .md file */
        if (info.type != LFS_TYPE_REG) {
            continue;
        }
        {
            size_t name_len = strlen(info.name);
            if (name_len < 4 || strcmp(info.name + name_len - 3, ".md") != 0) {
                continue;
            }
        }

        snprintf(filepath, sizeof(filepath), "%s/%s", dir_path, info.name);

        ret = lfs_file_open(lfs, &file, filepath, LFS_O_RDONLY);
        if (ret < 0) {
            AXK_LOG_WARN("[axk_skill_loader] cannot open 技能file: %s\r\n", filepath);
            continue;
        }

        read_len = lfs_file_read(lfs, &file, filebuf, sizeof(filebuf) - 1);
        lfs_file_close(lfs, &file);

        if (read_len <= 0) {
            continue;
        }
        filebuf[read_len] = '\0';

        if (axk_skill_parse_frontmatter(filebuf, name, sizeof(name),
                                         trigger, sizeof(trigger),
                                         description, sizeof(description)) == 0) {
            if (axk_skill_register_external(name, description, trigger, filepath) == 0) {
                loaded++;
            }
        } else {
            AXK_LOG_WARN("[axk_skill_loader] parse 技能fileFAIL: %s\r\n", filepath);
        }
    }

    lfs_dir_close(lfs, &dir);

    if (loaded > 0) {
        AXK_LOG_INFO("[axk_skill_loader] from filesystemload %d  skills\r\n", loaded);
    }
    return loaded;
}

/**
 * @brief TODO: 描述axk_skill_loader_init的功能
 *
 * @return 0成功, -1失败
 */
int axk_skill_loader_init(void)
{
    memset(s_skills, 0, sizeof(s_skills));
    s_skill_count = 0;

    /* register built-in skill */
    axk_skill_register("time_skill", "time 查询技能", "time ", axk_skill_exec_time);
    axk_skill_register("gpio_skill", "GPIO ctrl技能", "GPIO", axk_skill_exec_gpio);
    axk_skill_register("file_skill", "file操作技能", "file", NULL);
    axk_skill_register("wifi_skill", "WiFimgr技能", "WiFi", axk_skill_exec_wifi);

    /* loadexternal skillsfile */
    axk_skill_load_from_fs();

    AXK_LOG_INFO("[axk_skill_loader] skill loaderinitok (%d  skills)\r\n", s_skill_count);
    return 0;
}

/**
 * @brief TODO: 描述str_contains_nocase的功能
 *
 * @param haystack TODO: 描述haystack
 * @param needle TODO: 描述needle
 * @return 0成功, -1失败
 */
static bool str_contains_nocase(const char *haystack, const char *needle);

/**
 * @brief TODO: 描述str_contains_nocase的功能
 *
 * @param haystack TODO: 描述haystack
 * @param needle TODO: 描述needle
 * @return 0成功, -1失败
 */
static bool str_contains_nocase(const char *haystack, const char *needle)
{
    size_t needle_len;

    if (!haystack || !needle || needle[0] == '\0') {
        return false;
    }

    needle_len = strlen(needle);
    while (*haystack != '\0') {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return true;
        }
        haystack++;
    }

    return false;
}

const char *axk_skill_match(const char *text)
{
    int i;

    if (!text) {
        return NULL;
    }

    for (i = 0; i < s_skill_count; i++) {
        if (s_skills[i].registered && str_contains_nocase(text, s_skills[i].trigger)) {
            return s_skills[i].name;
        }
    }

    return NULL;
}

/**
 * @brief TODO: 描述axk_skill_execute的功能
 *
 * @param name TODO: 描述name
 * @param args TODO: 描述args
 * @param out_buf TODO: 描述out_buf
 * @param out_size TODO: 描述out_size
 * @return 0成功, -1失败
 */
int axk_skill_execute(const char *name, const char *args, char *out_buf, size_t out_size)
{
    int i;

    if (!name || !out_buf || out_size == 0) {
        return -1;
    }

    for (i = 0; i < s_skill_count; i++) {
        if (s_skills[i].registered && strcmp(s_skills[i].name, name) == 0) {
            if (s_skills[i].is_external) {
                /* external skills：readfile body content 作为output  */
                lfs_t *lfs = axk_tool_files_get_lfs();
                if (lfs && s_skills[i].filepath[0] != '\0') {
                    lfs_file_t file;
                    char filebuf[AXK_SKILL_BODY_MAX + 256];
                    lfs_ssize_t read_len;
                    int ret;

                    ret = lfs_file_open(lfs, &file, s_skills[i].filepath, LFS_O_RDONLY);
                    if (ret < 0) {
                        snprintf(out_buf, out_size, "cannot read技能file: %s", s_skills[i].filepath);
                        return -1;
                    }

                    read_len = lfs_file_read(lfs, &file, filebuf, sizeof(filebuf) - 1);
                    lfs_file_close(lfs, &file);

                    if (read_len > 0) {
                        filebuf[read_len] = '\0';
                        axk_skill_extract_body(filebuf, out_buf, out_size);
                        return 0;
                    }
                }
                snprintf(out_buf, out_size, "技能 '%s' filereadFAIL", name);
                return -1;
            } else {
                /* builtin 技能 */
                if (s_skills[i].exec) {
                    return s_skills[i].exec(args, out_buf, out_size);
                } else {
                    snprintf(out_buf, out_size, "技能 '%s' 尚not 实现执行体", name);
                    return -1;
                }
            }
        }
    }

    snprintf(out_buf, out_size, "not 找 to 技能 '%s'", name);
    return -1;
}

/**
 * @brief TODO: 描述axk_skill_get_prompt的功能
 *
 * @param name TODO: 描述name
 * @param buf TODO: 描述buf
 * @param buf_size TODO: 描述buf_size
 * @return 0成功, -1失败
 */
int axk_skill_get_prompt(const char *name, char *buf, size_t buf_size)
{
    int i;

    if (!name || !buf || buf_size == 0) {
        return -1;
    }

    for (i = 0; i < s_skill_count; i++) {
        if (s_skills[i].registered && s_skills[i].is_external &&
            strcmp(s_skills[i].name, name) == 0) {
            lfs_t *lfs = axk_tool_files_get_lfs();
            if (lfs && s_skills[i].filepath[0] != '\0') {
                lfs_file_t file;
                char filebuf[AXK_SKILL_BODY_MAX + 256];
                lfs_ssize_t read_len;
                int ret;

                ret = lfs_file_open(lfs, &file, s_skills[i].filepath, LFS_O_RDONLY);
                if (ret < 0) {
                    return -1;
                }

                read_len = lfs_file_read(lfs, &file, filebuf, sizeof(filebuf) - 1);
                lfs_file_close(lfs, &file);

                if (read_len > 0) {
                    filebuf[read_len] = '\0';
                    axk_skill_extract_body(filebuf, buf, buf_size);
                    return 0;
                }
            }
            return -1;
        }
    }

    return -1;
}
