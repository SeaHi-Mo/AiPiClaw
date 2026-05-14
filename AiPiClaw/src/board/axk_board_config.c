/**
 * @file axk_board_config.c
 * @brief board.json 解析器实现 - cJSON 解析 + bin2obj 嵌入数据
 * @version 1.0
 * @date 2026-05-14
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note boards/board.json 由 CMake bin2obj 嵌入为 _binary_boards_board_json_start/end
 */

#include "axk_board_config.h"
#include "axk_platform.h"
#include "cJSON.h"

#include <string.h>
#include <stdio.h>

/* ── bin2obj 外部符号 ───────────────────────────────── */
extern const uint8_t _binary_boards_board_json_start[];
extern const uint8_t _binary_boards_board_json_end[];

/* ── 解析后的内部状态 ──────────────────────────────── */
static axk_board_config_t s_cfg;
static bool s_initialized = false;
static axk_board_alias_entry_t s_aliases[AXK_BOARD_MAX_ALIASES];
static int s_alias_count = 0;
static int s_allowed_pins[AXK_BOARD_MAX_ALLOWED];
static int s_allowed_count = 0;
static int s_led_pins[AXK_BOARD_MAX_ALLOWED];
static int s_led_count = 0;

/* ── 硬编码 fallback (AiPi-Eyes-DU) ─────────────────── */
static void fill_fallback_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strncpy(s_cfg.board_name, "AiPi-Eyes-DU", AXK_BOARD_NAME_MAX - 1);
    strncpy(s_cfg.chip_model, "BL618", AXK_CHIP_MODEL_MAX - 1);
    s_cfg.chip_freq_mhz = 480;
    s_cfg.sram_kb = 512;
    s_cfg.psram_mb = 4;
    s_cfg.flash_mb = 4;
    strncpy(s_cfg.sdk_version, "bouffalo_sdk_v2.0.x", AXK_VERSION_MAX - 1);
    strncpy(s_cfg.fw_version, "v0.8.0-dev", AXK_VERSION_MAX - 1);
    strncpy(s_cfg.led_polarity, "active_high", AXK_POLARITY_MAX - 1);

    /* fallback 白名单 */
    s_allowed_count = 0;
    {
        const int defaults[] = { 10, 11, 12, 14, 15, 17 };
        int i;
        for (i = 0; i < (int)(sizeof(defaults)/sizeof(defaults[0])) && s_allowed_count < AXK_BOARD_MAX_ALLOWED; i++) {
            s_allowed_pins[s_allowed_count++] = defaults[i];
        }
    }

    /* fallback LED 引脚 */
    s_led_count = 0;
    {
        const int leds[] = { 12, 14, 15 };
        int i;
        for (i = 0; i < (int)(sizeof(leds)/sizeof(leds[0])) && s_led_count < AXK_BOARD_MAX_ALLOWED; i++) {
            s_led_pins[s_led_count++] = leds[i];
        }
    }

    /* fallback 别名 */
    s_alias_count = 0;
    {
        const struct { const char *name; uint8_t pin; uint8_t active; uint8_t flags; const char *desc; } defs[] = {
            { "red_led",   12, 1, 3, "红灯(GPIO12,高电平)" },
            { "红灯",       12, 1, 3, "红灯(GPIO12,高电平)" },
            { "led_r",     12, 1, 3, "红色LED(GPIO12)" },
            { "green_led", 14, 1, 3, "绿灯(GPIO14,高电平)" },
            { "绿灯",       14, 1, 3, "绿灯(GPIO14,高电平)" },
            { "led_g",     14, 1, 3, "绿色LED(GPIO14)" },
            { "blue_led",  15, 1, 3, "蓝灯(GPIO15,高电平)" },
            { "蓝灯",       15, 1, 3, "蓝灯(GPIO15,高电平)" },
            { "led_b",     15, 1, 3, "蓝色LED(GPIO15)" },
            { "key_0",     10, 1, 2, "KEY_0按键(GPIO10,只读)" },
            { "key_1",     11, 1, 2, "KEY_1按键(GPIO11,只读)" },
        };
        int i;
        for (i = 0; i < (int)(sizeof(defs)/sizeof(defs[0])) && s_alias_count < AXK_BOARD_MAX_ALIASES; i++) {
            strncpy(s_aliases[s_alias_count].name, defs[i].name, 23);
            s_aliases[s_alias_count].pin = defs[i].pin;
            s_aliases[s_alias_count].active_level = defs[i].active;
            s_aliases[s_alias_count].flags = defs[i].flags;
            strncpy(s_aliases[s_alias_count].desc, defs[i].desc, 63);
            s_alias_count++;
        }
    }
}

/**
 * @brief 安全获取 cJSON 字符串字段
 */
static const char *json_get_str(cJSON *obj, const char *key, const char *def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsString(item)) ? item->valuestring : def;
}

/**
 * @brief 安全获取 cJSON 整数字段
 */
static int json_get_int(cJSON *obj, const char *key, int def)
{
    cJSON *item = cJSON_GetObjectItem(obj, key);
    return (item && cJSON_IsNumber(item)) ? item->valueint : def;
}

/**
 * @brief 从 board.json 解析别名条目
 */
static void parse_alias_entry(cJSON *entry, int default_pin,
                              const char *def_name, uint8_t def_active,
                              uint8_t def_flags, const char *def_desc)
{
    if (s_alias_count >= AXK_BOARD_MAX_ALIASES) return;

    axk_board_alias_entry_t *a = &s_aliases[s_alias_count];
    memset(a, 0, sizeof(*a));

    /* pin */
    a->pin = (uint8_t)(entry ? json_get_int(entry, "gpio", default_pin) : default_pin);

    /* alias_en (english alias) */
    if (entry) {
        const char *alias_en = json_get_str(entry, "alias_en", NULL);
        if (alias_en && alias_en[0]) {
            strncpy(a->name, alias_en, 23);
            a->name[23] = '\0';
        }
    }
    if (!a->name[0] && def_name) {
        strncpy(a->name, def_name, 23);
        a->name[23] = '\0';
    }

    /* active_level: default active_high=1, active_low=0 */
    if (entry) {
        cJSON *active_high = cJSON_GetObjectItem(entry, "active_high");
        if (cJSON_IsBool(active_high)) {
            a->active_level = cJSON_IsTrue(active_high) ? 1 : 0;
        } else {
            /* 从全局 leds.polarity 推断 */
            if (strcmp(s_cfg.led_polarity, "active_low") == 0) {
                a->active_level = 0;
            } else {
                a->active_level = def_active;
            }
        }
    } else {
        a->active_level = def_active;
    }

    /* flags */
    a->flags = def_flags;

    /* desc */
    if (entry) {
        const char *desc = json_get_str(entry, "desc", NULL);
        if (desc && desc[0]) {
            strncpy(a->desc, desc, 63);
            a->desc[63] = '\0';
        }
    }
    if (!a->desc[0] && def_desc) {
        strncpy(a->desc, def_desc, 63);
        a->desc[63] = '\0';
    }

    s_alias_count++;
}

/**
 * @brief 从 pinmap 条目解析别名
 */
static void parse_pinmap_aliases(cJSON *pinmap_entry)
{
    int pin = json_get_int(pinmap_entry, "pin", -1);
    cJSON *aliases = cJSON_GetObjectItem(pinmap_entry, "aliases");
    cJSON *dir = cJSON_GetObjectItem(pinmap_entry, "direction");
    const char *direction = (dir && cJSON_IsString(dir)) ? dir->valuestring : "in";
    uint8_t flags = 0;

    if (pin < 0 || !aliases || !cJSON_IsArray(aliases)) return;

    /* 确定 flags: read always, write for 'out' direction */
    flags = 2; /* READ */
    if (direction && strcmp(direction, "out") == 0) {
        flags |= 1; /* WRITE */
    }

    /* 遍历 aliases 数组 */
    int count = cJSON_GetArraySize(aliases);
    for (int i = 0; i < count && s_alias_count < AXK_BOARD_MAX_ALIASES; i++) {
        cJSON *name_item = cJSON_GetArrayItem(aliases, i);
        if (!name_item || !cJSON_IsString(name_item)) continue;

        axk_board_alias_entry_t *a = &s_aliases[s_alias_count];
        memset(a, 0, sizeof(*a));
        strncpy(a->name, name_item->valuestring, 23);
        a->name[23] = '\0';
        a->pin = (uint8_t)pin;
        a->active_level = 1;
        a->flags = flags;

        /* 生成描述 */
        cJSON *funcs = cJSON_GetObjectItem(pinmap_entry, "functions");
        snprintf(a->desc, 63, "GPIO%d (%s)", pin,
                 (funcs && cJSON_IsArray(funcs) && cJSON_GetArraySize(funcs) > 0
                  && cJSON_GetArrayItem(funcs, 0) && cJSON_IsString(cJSON_GetArrayItem(funcs, 0)))
                  ? cJSON_GetArrayItem(funcs, 0)->valuestring : "GPIO");
        a->desc[63] = '\0';

        s_alias_count++;
    }
}

int axk_board_config_init(void)
{
    if (s_initialized) return 0;

    memset(&s_cfg, 0, sizeof(s_cfg));
    s_alias_count = 0;
    s_allowed_count = 0;
    s_led_count = 0;
    memset(s_aliases, 0, sizeof(s_aliases));
    memset(s_allowed_pins, 0, sizeof(s_allowed_pins));
    memset(s_led_pins, 0, sizeof(s_led_pins));

    /* 获取嵌入的 board.json 数据 */
    const char *json_start = (const char *)_binary_boards_board_json_start;
    const char *json_end   = (const char *)_binary_boards_board_json_end;
    ptrdiff_t json_len = json_end - json_start;

    if (json_len <= 0 || json_len > AXK_BOARD_JSON_MAX) {
        AXK_LOG_WARN("[board_config] embedded board.json invalid (len=%d), using fallback", (int)json_len);
        fill_fallback_defaults();
        s_initialized = true;
        return -1;
    }

    /* 复制到栈上（确保 null-terminated） */
    char json_buf[AXK_BOARD_JSON_MAX + 1];
    size_t copy_len = (size_t)json_len < AXK_BOARD_JSON_MAX ? (size_t)json_len : AXK_BOARD_JSON_MAX;
    memcpy(json_buf, json_start, copy_len);
    json_buf[copy_len] = '\0';

    cJSON *root = cJSON_Parse(json_buf);
    if (!root) {
        AXK_LOG_WARN("[board_config] cJSON parse failed, using fallback");
        fill_fallback_defaults();
        s_initialized = true;
        return -1;
    }

    /* ── 解析基础信息 ── */
    strncpy(s_cfg.board_name, json_get_str(root, "board_name", "AiPi-Eyes-DU"), AXK_BOARD_NAME_MAX - 1);

    cJSON *chip = cJSON_GetObjectItem(root, "chip");
    if (chip) {
        strncpy(s_cfg.chip_model, json_get_str(chip, "model", "BL618"), AXK_CHIP_MODEL_MAX - 1);
        s_cfg.chip_freq_mhz = json_get_int(chip, "frequency_mhz", 480);
        s_cfg.sram_kb       = json_get_int(chip, "sram_kb", 512);
        s_cfg.psram_mb      = json_get_int(chip, "psram_mb", 4);
        s_cfg.flash_mb      = json_get_int(chip, "flash_mb", 4);
    } else {
        strncpy(s_cfg.chip_model, "BL618", AXK_CHIP_MODEL_MAX - 1);
        s_cfg.chip_freq_mhz = 480;
        s_cfg.sram_kb = 512;
        s_cfg.psram_mb = 4;
        s_cfg.flash_mb = 4;
    }

    cJSON *fw = cJSON_GetObjectItem(root, "firmware");
    if (fw) {
        strncpy(s_cfg.sdk_version, json_get_str(fw, "sdk", "bouffalo_sdk_v2.0.x"), AXK_VERSION_MAX - 1);
        strncpy(s_cfg.fw_version, json_get_str(fw, "version", "v0.8.0-dev"), AXK_VERSION_MAX - 1);
    } else {
        strncpy(s_cfg.sdk_version, "bouffalo_sdk_v2.0.x", AXK_VERSION_MAX - 1);
        strncpy(s_cfg.fw_version, "v0.8.0-dev", AXK_VERSION_MAX - 1);
    }

    cJSON *leds = cJSON_GetObjectItem(root, "leds");
    if (leds) {
        strncpy(s_cfg.led_polarity, json_get_str(leds, "polarity", "active_high"), AXK_POLARITY_MAX - 1);

        /* 解析 LED 列表 */
        cJSON *list = cJSON_GetObjectItem(leds, "list");
        if (list && cJSON_IsArray(list)) {
            int count = cJSON_GetArraySize(list);
            for (int i = 0; i < count && i < AXK_BOARD_MAX_ALLOWED; i++) {
                cJSON *item = cJSON_GetArrayItem(list, i);
                if (!item) continue;
                int gpio = json_get_int(item, "gpio", -1);
                if (gpio >= 0) {
                    s_led_pins[s_led_count++] = gpio;
                }
            }
        }
    } else {
        strncpy(s_cfg.led_polarity, "active_high", AXK_POLARITY_MAX - 1);
        /* fallback LEDs */
        const int fallback_leds[] = { 12, 14, 15 };
        for (int i = 0; i < 3 && s_led_count < AXK_BOARD_MAX_ALLOWED; i++) {
            s_led_pins[s_led_count++] = fallback_leds[i];
        }
    }

    /* ── 解析 pinmap → 别名 ── */
    cJSON *pinmap = cJSON_GetObjectItem(root, "pinmap");
    if (pinmap && cJSON_IsArray(pinmap)) {
        int count = cJSON_GetArraySize(pinmap);
        for (int i = 0; i < count && s_alias_count < AXK_BOARD_MAX_ALIASES; i++) {
            cJSON *entry = cJSON_GetArrayItem(pinmap, i);
            if (!entry) continue;
            parse_pinmap_aliases(entry);
        }
    }

    /* 补充解析 buttons（如果 pinmap 未覆盖） */
    cJSON *buttons = cJSON_GetObjectItem(root, "buttons");
    if (buttons && cJSON_IsArray(buttons)) {
        int count = cJSON_GetArraySize(buttons);
        for (int i = 0; i < count && s_alias_count < AXK_BOARD_MAX_ALIASES; i++) {
            cJSON *btn = cJSON_GetArrayItem(buttons, i);
            if (!btn) continue;
            parse_alias_entry(btn, 0, NULL, 1, 2, NULL);
        }
    }

    /* 补充解析 LED 别名（如果 pinmap 未覆盖） */
    if (leds) {
        cJSON *list = cJSON_GetObjectItem(leds, "list");
        if (list && cJSON_IsArray(list)) {
            int count = cJSON_GetArraySize(list);
            for (int i = 0; i < count && s_alias_count < AXK_BOARD_MAX_ALIASES; i++) {
                cJSON *led = cJSON_GetArrayItem(list, i);
                if (!led) continue;
                parse_alias_entry(led, 0, NULL, 1, 3, NULL);
            }
        }
    }

    /* ── 解析 gpio_default_policy.allow_pins ── */
    cJSON *policy = cJSON_GetObjectItem(root, "gpio_default_policy");
    if (policy) {
        cJSON *allow_pins = cJSON_GetObjectItem(policy, "allow_pins");
        if (allow_pins && cJSON_IsArray(allow_pins)) {
            int count = cJSON_GetArraySize(allow_pins);
            for (int i = 0; i < count && s_allowed_count < AXK_BOARD_MAX_ALLOWED; i++) {
                cJSON *p = cJSON_GetArrayItem(allow_pins, i);
                if (p && cJSON_IsNumber(p)) {
                    s_allowed_pins[s_allowed_count++] = p->valueint;
                }
            }
        }
    }

    /* 如果白名单为空，用 pinmap 中 allowed=true 的引脚填充 */
    if (s_allowed_count == 0 && pinmap && cJSON_IsArray(pinmap)) {
        int count = cJSON_GetArraySize(pinmap);
        for (int i = 0; i < count && s_allowed_count < AXK_BOARD_MAX_ALLOWED; i++) {
            cJSON *entry = cJSON_GetArrayItem(pinmap, i);
            if (!entry) continue;
            cJSON *allowed = cJSON_GetObjectItem(entry, "allowed");
            if (cJSON_IsTrue(allowed)) {
                int pin = json_get_int(entry, "pin", -1);
                if (pin >= 0) {
                    /* 避免重复 */
                    bool dup = false;
                    for (int j = 0; j < s_allowed_count; j++) {
                        if (s_allowed_pins[j] == pin) { dup = true; break; }
                    }
                    if (!dup) {
                        s_allowed_pins[s_allowed_count++] = pin;
                    }
                }
            }
        }
    }

    cJSON_Delete(root);

    s_initialized = true;
    AXK_LOG_INFO("[board_config] initialized: board=%s chip=%s, %d aliases, %d allowed pins",
                 s_cfg.board_name, s_cfg.chip_model, s_alias_count, s_allowed_count);
    return 0;
}

/* ── Getter 实现 ────────────────────────────────────── */

const axk_board_config_t *axk_board_config_get(void)
{
    return s_initialized ? &s_cfg : NULL;
}

const char *axk_board_config_get_json(void)
{
    if (!s_initialized) return NULL;
    const char *start = (const char *)_binary_boards_board_json_start;
    const char *end   = (const char *)_binary_boards_board_json_end;
    ptrdiff_t len = end - start;
    if (len <= 0 || len > AXK_BOARD_JSON_MAX) return NULL;
    return start;
}

int axk_board_config_get_led_pins(int *pins, int max)
{
    if (!pins || max <= 0 || !s_initialized) return 0;
    int n = (s_led_count < max) ? s_led_count : max;
    for (int i = 0; i < n; i++) {
        pins[i] = s_led_pins[i];
    }
    return n;
}

int axk_board_config_get_allowed_pins(int *pins, int max)
{
    if (!pins || max <= 0 || !s_initialized) return 0;
    int n = (s_allowed_count < max) ? s_allowed_count : max;
    for (int i = 0; i < n; i++) {
        pins[i] = s_allowed_pins[i];
    }
    return n;
}

int axk_board_config_lookup_alias(const char *name, uint8_t *pin_out)
{
    if (!name || !pin_out || !s_initialized) return -1;
    for (int i = 0; i < s_alias_count; i++) {
        if (strcmp(s_aliases[i].name, name) == 0) {
            *pin_out = s_aliases[i].pin;
            return 0;
        }
    }
    return -1;
}

int axk_board_config_get_alias_count(void)
{
    return s_alias_count;
}

int axk_board_config_get_aliases(axk_board_alias_entry_t *entries, int max)
{
    if (!entries || max <= 0 || !s_initialized) return 0;
    int n = (s_alias_count < max) ? s_alias_count : max;
    for (int i = 0; i < n; i++) {
        entries[i] = s_aliases[i];
    }
    return n;
}
