/**
 * @file axk_wifi_onboard.c
 * @brief WiFi provision module实现 - 基于 easyflash store + shell cmdconfig
 * @version 1.0
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 * @note support 串口cmdprovision 、auto save credential 、开机auto connect
 */

#include "axk_wifi_onboard.h"
#include "axk_platform.h"
#include "axk_wifi_manager.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "wifi_mgmr.h"
#include "easyflash.h"
#include "shell.h"

#define AXK_WIFI_KV_SSID     "mimi_wifi_ssid"
#define AXK_WIFI_KV_PASSWORD "mimi_wifi_pwd"

static bool s_onboard_active = false;

/**
 * @brief save WiFicredential  to easyflash
 * @param[in] ssid SSIDchars 串
 * @param[in] password password chars 串（可为NULL or empty chars 串）
 * @return OKreturn 0
 */
int axk_wifi_save_credentials(const char *ssid, const char *password)
{
    EfErrCode err;

    if (!ssid || ssid[0] == '\0') {
        return -1;
    }

    err = ef_set_env(AXK_WIFI_KV_SSID, ssid);
    if (err != EF_NO_ERR) {
        AXK_LOG_ERROR("[axk_wifi_onboard] save SSIDFAIL: %d\r\n", err);
        return -1;
    }

    err = ef_set_env(AXK_WIFI_KV_PASSWORD, password ? password : "");
    if (err != EF_NO_ERR) {
        AXK_LOG_ERROR("[axk_wifi_onboard] save password FAIL: %d\r\n", err);
        return -1;
    }

    ef_save_env();
    AXK_LOG_INFO("[axk_wifi_onboard] WiFicredential save \r\n");
    return 0;
}

/**
 * @brief from easyflashloadWiFicredential 
 * @param[out] ssid_buf SSIDoutput buffer 
 * @param[in] ssid_buf_len buffer length 
 * @param[out] pwd_buf password output buffer 
 * @param[in] pwd_buf_len buffer length 
 * @return OKreturn 0，not 找 to return -1
 */
static int axk_wifi_load_credentials(char *ssid_buf, size_t ssid_buf_len,
                                      char *pwd_buf, size_t pwd_buf_len)
{
    size_t len = 0;

    if (ef_get_env_blob(AXK_WIFI_KV_SSID, ssid_buf, ssid_buf_len, &len) == 0 || len == 0) {
        return -1;
    }
    ssid_buf[ssid_buf_len - 1] = '\0';

    if (ef_get_env_blob(AXK_WIFI_KV_PASSWORD, pwd_buf, pwd_buf_len, &len) == 0) {
        pwd_buf[0] = '\0';
    } else {
        pwd_buf[pwd_buf_len - 1] = '\0';
    }

    return 0;
}

/**
 * @brief attempting saved WiFi connect
 * @return OK发起connectreturn 0，无save credential return -1
 */
int axk_wifi_auto_connect(void)
{
    char ssid[64] = {0};
    char pwd[64] = {0};

    if (axk_wifi_load_credentials(ssid, sizeof(ssid), pwd, sizeof(pwd)) != 0) {
        AXK_LOG_INFO("[axk_wifi_onboard] 无save WiFicredential ，skip auto connect\r\n");
        return -1;
    }

    AXK_LOG_INFO("[axk_wifi_onboard] attempt auto connectWiFi: %s\r\n", ssid);
    return axk_wifi_connect(ssid, pwd[0] ? pwd : NULL);
}

/* @brief TODO: 描述axk_wifi_onboard_init的功能 @return 0成功, -1失败 */
int axk_wifi_onboard_init(void)
{
    s_onboard_active = false;
    AXK_LOG_INFO("[axk_wifi_onboard] WiFi provisioningmoduleinitok\r\n");

    /* 延迟一点再auto connect， etc待WiFifwstartok */
    /* 实际connect由userin shell执行 or main looptrigger  */
    return 0;
}

/**
 * @brief startprovision mode （预留SoftAP功能）
 */
int axk_wifi_onboard_start(void)
{
    if (s_onboard_active) {
        AXK_LOG_WARN("[axk_wifi_onboard] provision in 进行\r\n");
        return -1;
    }

    struct wifi_mgmr_ap_params ap_cfg = {0};
    int ret;

    s_onboard_active = true;
    AXK_LOG_INFO("[axk_wifi_onboard] startSoftAPprovision mode \r\n");

    ap_cfg.ssid = "MimiClaw-Config";
    ap_cfg.key = "12345678";
    ap_cfg.akm = "WPA2";
    ap_cfg.channel = 6;
    ap_cfg.use_dhcpd = true;
    ap_cfg.start = 2;
    ap_cfg.limit = 4;

    ret = wifi_mgmr_ap_start(&ap_cfg);
    if (ret != 0) {
        AXK_LOG_ERROR("[axk_wifi_onboard] startSoftAPFAIL: %d\r\n", ret);
        s_onboard_active = false;
        return -1;
    }

    AXK_LOG_INFO("[axk_wifi_onboard] SoftAPstarted\r\n");
    AXK_LOG_INFO("[axk_wifi_onboard] SSID: MimiClaw-Config, password : 12345678\r\n");
    AXK_LOG_INFO("[axk_wifi_onboard] 请connect热点后via CLIcmd wifi_set config\r\n");
    return 0;
}

/* @brief TODO: 描述axk_wifi_onboard_stop的功能 @return 无返回值 */
void axk_wifi_onboard_stop(void)
{
    if (!s_onboard_active) {
        return;
    }
    s_onboard_active = false;
    AXK_LOG_INFO("[axk_wifi_onboard] provision mode stop \r\n");
}

/* @brief TODO: 描述axk_wifi_onboard_poll的功能 @return 无返回值 */
void axk_wifi_onboard_poll(void)
{
    /* provision status poll ，current 无需process */
}

/* ===================== Shell cmd ===================== */

/**
 * @brief Shellcmd：wifi_set <ssid> [password]
 * @note set WiFicredential  and immediate attempt connect
 */
static void cmd_wifi_set(int argc, char **argv)
{
    if (argc < 2) {
        printf("Usage: wifi_set <ssid> [password]\r\n");
        printf("Example: wifi_set MyHomeWiFi 12345678\r\n");
        printf("         wifi_set OpenNetwork\r\n");
        return;
    }

    const char *ssid = argv[1];
    const char *pwd = (argc >= 3) ? argv[2] : "";

    if (axk_wifi_save_credentials(ssid, pwd) != 0) {
        printf("[wifi_set] Save WiFi credentials FAILED\r\n");
        return;
    }

    printf("[wifi_set] Credentials saved, connecting to %s ...\r\n", ssid);

    int ret = axk_wifi_connect(ssid, pwd[0] ? pwd : NULL);
    if (ret != 0) {
        printf("[wifi_set] Connect request FAILED: %d\r\n", ret);
    } else {
        printf("[wifi_set] Connect request sent\r\n");
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_set, wifi_set, set WiFi SSID & password and connect);

/**
 * @brief Shellcmd：wifi_status
 * @note show current WiFiconnectstatus 
 */
static void cmd_wifi_status(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    axk_wifi_state_t state = axk_wifi_get_state();
    const char *state_str = "Unknown";
    switch (state) {
        case AXK_WIFI_STATE_DISCONNECTED: state_str = "Disconnected"; break;
        case AXK_WIFI_STATE_CONNECTING:   state_str = "Connecting"; break;
        case AXK_WIFI_STATE_CONNECTED:    state_str = "Connected"; break;
        case AXK_WIFI_STATE_GOT_IP:       state_str = "Got IP"; break;
    }

    printf("[wifi_status] Status: %s\r\n", state_str);

    if (state == AXK_WIFI_STATE_GOT_IP) {
        char ip[32] = {0};
        char ssid[64] = {0};
        int rssi = 0;

        if (axk_wifi_get_ip(ip, sizeof(ip)) == 0) {
            printf("[wifi_status] IP: %s\r\n", ip);
        }
        if (axk_wifi_get_ssid(ssid, sizeof(ssid)) == 0) {
            printf("[wifi_status] SSID: %s\r\n", ssid);
        }
        if (axk_wifi_get_rssi(&rssi) == 0) {
            printf("[wifi_status] RSSI: %d dBm\r\n", rssi);
        }
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_status, wifi_status, show WiFi connection status);

/**
 * @brief Shellcmd：wifi_disconnect
 * @note disconnectcurrent WiFiconnect
 */
static void cmd_wifi_disconnect(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int ret = axk_wifi_disconnect();
    if (ret == 0) {
        printf("[wifi_disconnect] WiFi disconnected\r\n");
    } else {
        printf("[wifi_disconnect] Disconnect FAILED: %d\r\n", ret);
    }
}
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_disconnect, wifi_disconnect, disconnect WiFi);
