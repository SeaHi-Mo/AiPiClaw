/**
 * @file axk_hal_gpio.c
 * @brief GPIO硬件抽象层实现 - 基于宏元编程平台解耦
 * @version 1.0
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note via 条件编译实现BL618&ESP32GPIO适配
 */

#include "axk_hal_gpio.h"
#include <stdio.h>
#include <string.h>

/* ============================================================
 * 平台特定实现
 * ============================================================ */
#if AXK_PLATFORM_BL618
    #include "bflb_gpio.h"
    #include "bflb_core.h"
    static struct bflb_device_s* axk_gpio_dev = NULL;  /**< BL618 GPIOdevice handle  */

    /* BL618 HARDWARE BUG: bflb_gpio_init() writes OE to I2S_CFG0 instead of
     * GPIO CFG register. Must manually set OE bit in GLB GPIO CFG register
     * (0x200008C4 + (pin>>1)*4, bit 6+(pin&1)*16) after each init. */
    static void axk_gpio_fix_oe(uint8_t pin)
    {
        volatile uint32_t *cfg = (volatile uint32_t *)
            (0x200008C4 + ((uint32_t)(pin >> 1) << 2));
        uint32_t bit = 6 + ((pin & 1) << 4);  /* 6 for even, 22 for odd */
        *cfg |= (1U << bit);
    }

    static inline struct bflb_device_s* axk_gpio_get_dev(void)
    {
        if (axk_gpio_dev == NULL) {
            axk_gpio_dev = bflb_device_get_by_name("gpio");
        }
        return axk_gpio_dev;
    }

#elif AXK_PLATFORM_ESP32
    #include "driver/gpio.h"
#endif

/* ============================================================
 * 平台无关公共实现
 * ============================================================ */

int axk_hal_gpio_init(void)
{
    AXK_LOG_INFO("[axk_hal_gpio] initGPIO HAL\r\n");
#if AXK_PLATFORM_BL618
    axk_gpio_get_dev(); /* initdevice handle  */

    /* Init RGB LED pins (common anode: 0=ON, 1=OFF).
     * GPIO12=Red, GPIO14=Green, GPIO15=Blue. OUTPUT|FLOAT|DRV_3. */
    {
        axk_gpio_cfg_t led_cfg = {
            .mode = AXK_GPIO_MODE_OUT,
            .pull = AXK_GPIO_PULL_NONE,
            .drive = AXK_GPIO_DRIVE_STRONG
        };
        axk_hal_gpio_config(12, &led_cfg);
        axk_hal_gpio_config(14, &led_cfg);
        axk_hal_gpio_config(15, &led_cfg);
    }
#endif
    return 0;
}

/**
 * @brief set GPIOpin 方 to 
 * @param[in] pin pin 号
 * @param[in] mode 方 to mode  (AXK_GPIO_MODE_IN / AXK_GPIO_MODE_OUT / ...)
 * @return OKreturn 0
 */
int axk_hal_gpio_set_direction(uint32_t pin, uint32_t mode)
{
    AXK_LOG_DEBUG("[axk_hal_gpio] \u8bbe\u7f6e\u5f15\u811a%d\u65b9\u5411\u4e3a%d\\r\\n", (int)pin, (int)mode);

#if AXK_PLATFORM_BL618
    uint32_t cfgset = GPIO_FUNC_GPIO | GPIO_FLOAT;
    if (mode == AXK_GPIO_MODE_OUT) {
        cfgset |= GPIO_OUTPUT;
    } else if (mode == AXK_GPIO_MODE_AF) {
        cfgset |= GPIO_ALTERNATE;
    } else if (mode == AXK_GPIO_MODE_OD) {
        cfgset |= GPIO_OUTPUT; /* BL618 \u65e0\u4e13\u7528\u5f00\u6f0f\u6a21\u5f0f\uff0c\u4f7f\u7528\u666e\u901a\u8f93\u51fa */
    } else {
        cfgset |= GPIO_INPUT;
    }
    bflb_gpio_init(axk_gpio_get_dev(), (uint8_t)pin, cfgset);
    if (mode == AXK_GPIO_MODE_OUT || mode == AXK_GPIO_MODE_OD) {
        axk_gpio_fix_oe((uint8_t)pin);
    }
#elif AXK_PLATFORM_ESP32
    gpio_set_direction((int)pin,
        (mode == AXK_GPIO_MODE_IN) ? GPIO_MODE_INPUT :
        (mode == AXK_GPIO_MODE_OUT) ? GPIO_MODE_OUTPUT :
        (mode == AXK_GPIO_MODE_OD) ? GPIO_MODE_OUTPUT_OD : GPIO_MODE_INPUT_OUTPUT);
#endif
    return 0;
}

int axk_hal_gpio_config(uint32_t pin, const axk_gpio_cfg_t* cfg)
{
    if (cfg == NULL) {
        AXK_LOG_ERROR("[axk_hal_gpio] \u914d\u7f6e\u53c2\u6570\u4e3a\u7a7a\\r\\n");
        return -1;
    }

    AXK_LOG_DEBUG("[axk_hal_gpio] \u914d\u7f6e\u5f15\u811a%d, \u6a21\u5f0f%d\\r\\n", (int)pin, (int)cfg->mode);

#if AXK_PLATFORM_BL618
    uint32_t cfgset = GPIO_FUNC_GPIO;

    if (cfg->mode == AXK_GPIO_MODE_OUT) {
        cfgset |= GPIO_OUTPUT;
    } else if (cfg->mode == AXK_GPIO_MODE_AF) {
        cfgset |= GPIO_ALTERNATE;
    } else if (cfg->mode == AXK_GPIO_MODE_OD) {
        cfgset |= GPIO_OUTPUT;
    } else {
        cfgset |= GPIO_INPUT;
    }

    if (cfg->pull == AXK_GPIO_PULL_UP) {
        cfgset |= GPIO_PULLUP;
    } else if (cfg->pull == AXK_GPIO_PULL_DOWN) {
        cfgset |= GPIO_PULLDOWN;
    } else {
        cfgset |= GPIO_FLOAT;
    }

    if (cfg->drive == AXK_GPIO_DRIVE_STRONG) {
        cfgset |= GPIO_DRV_3;
    } else {
        cfgset |= GPIO_DRV_0;
    }

    bflb_gpio_init(axk_gpio_get_dev(), (uint8_t)pin, cfgset);
    if (cfg->mode == AXK_GPIO_MODE_OUT || cfg->mode == AXK_GPIO_MODE_OD) {
        axk_gpio_fix_oe((uint8_t)pin);
    }
#elif AXK_PLATFORM_ESP32
    gpio_config_t io_conf = {0};
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = (cfg->mode == AXK_GPIO_MODE_IN) ? GPIO_MODE_INPUT :
                   (cfg->mode == AXK_GPIO_MODE_OUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_OUTPUT_OD;
    io_conf.pull_up_en = (cfg->pull == AXK_GPIO_PULL_UP) ? 1 : 0;
    io_conf.pull_down_en = (cfg->pull == AXK_GPIO_PULL_DOWN) ? 1 : 0;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);
#endif
    return 0;
}

int axk_hal_gpio_set_level(uint32_t pin, uint32_t level)
{
#if AXK_PLATFORM_BL618
    if (level) {
        bflb_gpio_set(axk_gpio_get_dev(), (uint8_t)pin);
    } else {
        bflb_gpio_reset(axk_gpio_get_dev(), (uint8_t)pin);
    }
#elif AXK_PLATFORM_ESP32
    gpio_set_level((int)pin, (int)level);
#endif
    return 0;
}

int axk_hal_gpio_get_level(uint32_t pin)
{
#if AXK_PLATFORM_BL618
    return bflb_gpio_read(axk_gpio_get_dev(), (uint8_t)pin) ? 1 : 0;
#elif AXK_PLATFORM_ESP32
    return gpio_get_level((int)pin);
#else
    return 0;
#endif
}

int axk_hal_gpio_toggle(uint32_t pin)
{
#if AXK_PLATFORM_BL618
    int level = axk_hal_gpio_get_level(pin);
    if (level) {
        bflb_gpio_reset(axk_gpio_get_dev(), (uint8_t)pin);
    } else {
        bflb_gpio_set(axk_gpio_get_dev(), (uint8_t)pin);
    }
#elif AXK_PLATFORM_ESP32
    gpio_set_level((int)pin, !gpio_get_level((int)pin));
#endif
    return 0;
}

/* ============================================================
 * 断mgr - 平台特定实现
 * ============================================================ */

#if AXK_PLATFORM_BL618
    static axk_gpio_isr_cb_t s_gpio_isr_cbs[32] = {NULL};  /**< per pin callback  */
    static void* s_gpio_isr_args[32] = {NULL};             /**< per pin param  */
    static uint64_t s_gpio_last_int_time[32] = {0};        /**< 去抖动time 戳 */
    #define AXK_GPIO_DEBOUNCE_MS  50  /**< 去抖动窗口(ms) */

    static void axk_gpio_isr_callback(uint8_t pin)
    {
        uint64_t now;
        struct bflb_device_s *dev = axk_gpio_get_dev();
        if (!dev) return;

        /* 去抖动: 50ms 内重复trigger ignore  */
#if AXK_PLATFORM_BL618
        now = bflb_mtimer_get_time_ms();
#else
        now = 0;
#endif
        if (now - s_gpio_last_int_time[pin] < AXK_GPIO_DEBOUNCE_MS) {
            bflb_gpio_int_clear(dev, pin);
            return;
        }
        s_gpio_last_int_time[pin] = now;

        bflb_gpio_int_clear(dev, pin);
        if (s_gpio_isr_cbs[pin]) {
            s_gpio_isr_cbs[pin](pin, s_gpio_isr_args[pin]);
        }
    }
#endif

int axk_hal_gpio_set_interrupt(uint32_t pin, uint32_t trigger, axk_gpio_isr_cb_t cb, void* arg)
{
#if AXK_PLATFORM_BL618
    struct bflb_device_s *dev = axk_gpio_get_dev();
    uint32_t bflb_trig = GPIO_INT_TRIG_MODE_SYNC_FALLING_EDGE;

    if (!dev || pin > 31) return -1;

    if (trigger == AXK_GPIO_INTR_POSEDGE) {
        bflb_trig = GPIO_INT_TRIG_MODE_SYNC_RISING_EDGE;
    } else if (trigger == AXK_GPIO_INTR_NEGEDGE) {
        bflb_trig = GPIO_INT_TRIG_MODE_SYNC_FALLING_EDGE;
    } else if (trigger == AXK_GPIO_INTR_ANYEDGE) {
        bflb_trig = GPIO_INT_TRIG_MODE_SYNC_FALLING_RISING_EDGE;
    } else if (trigger == AXK_GPIO_INTR_LOW_LEVEL) {
        bflb_trig = GPIO_INT_TRIG_MODE_SYNC_LOW_LEVEL;
    } else if (trigger == AXK_GPIO_INTR_HIGH_LEVEL) {
        bflb_trig = GPIO_INT_TRIG_MODE_SYNC_HIGH_LEVEL;
    }

    s_gpio_isr_cbs[pin] = cb;
    s_gpio_isr_args[pin] = arg;

    bflb_gpio_int_mask(dev, (uint8_t)pin, true); /* 先disable ，configok后再enabled */
    bflb_gpio_irq_attach((uint8_t)pin, axk_gpio_isr_callback);
    bflb_gpio_int_init(dev, (uint8_t)pin, bflb_trig);
    return 0;
#elif AXK_PLATFORM_ESP32
    gpio_int_type_t intr_type = GPIO_INTR_DISABLE;
    if (trigger == AXK_GPIO_INTR_POSEDGE) {
        intr_type = GPIO_INTR_POSEDGE;
    } else if (trigger == AXK_GPIO_INTR_NEGEDGE) {
        intr_type = GPIO_INTR_NEGEDGE;
    } else if (trigger == AXK_GPIO_INTR_ANYEDGE) {
        intr_type = GPIO_INTR_ANYEDGE;
    } else if (trigger == AXK_GPIO_INTR_LOW_LEVEL) {
        intr_type = GPIO_INTR_LOW_LEVEL;
    } else if (trigger == AXK_GPIO_INTR_HIGH_LEVEL) {
        intr_type = GPIO_INTR_HIGH_LEVEL;
    }
    gpio_set_intr_type((int)pin, intr_type);
    gpio_isr_handler_add((int)pin, (gpio_isr_t)cb, arg);
    return 0;
#else
    return -1;
#endif
}

int axk_hal_gpio_intr_enable(uint32_t pin)
{
#if AXK_PLATFORM_BL618
    struct bflb_device_s *dev = axk_gpio_get_dev();
    if (dev && pin <= 31) {
        bflb_gpio_int_mask(dev, (1U << pin), false);
        return 0;
    }
#elif AXK_PLATFORM_ESP32
    gpio_intr_enable((int)pin);
    return 0;
#endif
    return -1;
}

int axk_hal_gpio_intr_disable(uint32_t pin)
{
#if AXK_PLATFORM_BL618
    struct bflb_device_s *dev = axk_gpio_get_dev();
    if (dev && pin <= 31) {
        bflb_gpio_int_mask(dev, (1U << pin), true);
        return 0;
    }
#elif AXK_PLATFORM_ESP32
    gpio_intr_disable((int)pin);
    return 0;
#endif
    return -1;
}

/* ── 上下拉config ────────────────────────────────── */

int axk_hal_gpio_set_pull(uint32_t pin, uint32_t pull)
{
#if AXK_PLATFORM_BL618
    struct bflb_device_s *dev = axk_gpio_get_dev();
    uint32_t cfgset;
    int current;

    if (!dev) return -1;

    current = bflb_gpio_read(dev, (uint8_t)pin);
    cfgset = GPIO_FUNC_GPIO;
    cfgset |= current ? GPIO_OUTPUT : GPIO_INPUT;

    if (pull == AXK_GPIO_PULL_UP) {
        cfgset |= GPIO_PULLUP;
    } else if (pull == AXK_GPIO_PULL_DOWN) {
        cfgset |= GPIO_PULLDOWN;
    } else {
        cfgset |= GPIO_FLOAT;
    }

    bflb_gpio_init(dev, (uint8_t)pin, cfgset);
    if (current) {
        axk_gpio_fix_oe((uint8_t)pin);
    }
    return 0;
#else
    (void)pin; (void)pull;
    return -1;
#endif
}

int axk_hal_gpio_get_pull(uint32_t pin)
{
#if AXK_PLATFORM_BL618
    (void)pin;
    return AXK_GPIO_PULL_NONE;  /**< SDK 无直接查询API */
#else
    (void)pin;
    return -1;
#endif
}
