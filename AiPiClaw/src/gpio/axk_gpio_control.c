/**
 * @file axk_gpio_control.c
 * @brief IO8key 控制：短按唤醒 / 长按3srestore 出厂set 
 * @version 1.1
 * @date 2026-04-20
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note 
 *   - IO8 短按: 硬件PDS/HBN唤醒（由LPfwconfig，无需本moduleprocess）
 *   - IO8 长按(>3s): erase EasyFlashstore + systemreset 
 *   - status 机in  axk_gpio_control_poll() 运行，需main loop周期call 
 */

#include "axk_gpio_control.h"
#include "axk_platform.h"
#include "axk_hal_system.h"
#include <stdbool.h>

/* ============================================================
 * 平台特定include 
 * ============================================================ */
#if AXK_PLATFORM_BL618
    #include "bflb_gpio.h"
    #include "bflb_mtimer.h"
    #include "easyflash.h"
#endif

/* ============================================================
 * 静态变量
 * ============================================================ */

/** GPIOdevice handle  */
#if AXK_PLATFORM_BL618
static struct bflb_device_s *s_gpio_dev = NULL;
#endif

/** current key status  */
static axk_btn_state_t s_btn_state = AXK_BTN_IDLE;

/** key 按下时刻(ms) */
static uint32_t s_btn_press_start_ms = 0;

/* ============================================================
 * 实现
 * ============================================================ */

int axk_gpio_control_init(void)
{
#if AXK_PLATFORM_BL618
    /* get GPIOdevice handle  */
    s_gpio_dev = bflb_device_get_by_name("gpio");

    /* IO8 config为input  + 上拉（key 另一端接GND，按下为低）
     * bflb_gpio_init(dev, pin, cfgset): cfgset = INPUT | PULLUP | DRV */
    bflb_gpio_init(s_gpio_dev, AXK_FACTORY_RESET_BTN_PIN,
                   GPIO_INPUT | GPIO_PULLUP | GPIO_DRV_3);

    AXK_LOG_INFO("[gpio_control] IO%d factory reset button initialized, hold %dms to reset\r\n",
                 AXK_FACTORY_RESET_BTN_PIN, AXK_FACTORY_RESET_HOLD_MS);
#else
    AXK_LOG_WARN("[gpio_control] Button not implemented for this platform\r\n");
#endif
    return 0;
}

void axk_gpio_control_poll(void)
{
#if AXK_PLATFORM_BL618
    if (s_gpio_dev == NULL) return;  /* not initialized yet */
    bool pressed = bflb_gpio_read(s_gpio_dev, AXK_FACTORY_RESET_BTN_PIN);
    uint32_t now = bflb_mtimer_get_time_ms();

    switch (s_btn_state) {

    case AXK_BTN_IDLE:
        if (!pressed) {
            s_btn_state = AXK_BTN_PRESSING;
            s_btn_press_start_ms = now;
        }
        break;

    case AXK_BTN_PRESSING:
        if (pressed) {
            /* release 时not  to 阈value  -> 短按，仅唤醒，not process */
            AXK_LOG_DEBUG("[gpio_control] Short press, wake only\r\n");
            s_btn_state = AXK_BTN_IDLE;
        } else if ((now - s_btn_press_start_ms) >= AXK_FACTORY_RESET_HOLD_MS) {
            /* 达 to 长按阈value  -> trigger restore 出厂 */
            s_btn_state = AXK_BTN_FACTORY_RESET;
        }
        break;

    case AXK_BTN_FACTORY_RESET:
        AXK_LOG_INFO("========================================\r\n");
        AXK_LOG_INFO("[gpio_control] FACTORY RESET TRIGGERED!\r\n");
        AXK_LOG_INFO("[gpio_control] Erasing user settings...\r\n");
        ef_env_set_default();
        ef_save_env();
        AXK_LOG_INFO("[gpio_control] System restarting in 500ms...\r\n");
        AXK_LOG_INFO("========================================\r\n");
        bflb_mtimer_delay_ms(500);
        axk_hal_system_reset();
        break;
    }
#endif
}
