/**
 * @file axk_captive_portal.c
 * @brief Captive Portal redirect module implementation
 * @version 1.0
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note Provides HTTP 302 redirect response and manages the integrated
 *       Captive Portal services lifecycle (DNS hijack + DHCP option).
 *
 * Integration points:
 *   - In axk_config_portal.c handle_request(), call
 *     axk_captive_portal_redirect() for unmatched GET routes
 *   - In axk_config_portal.c axk_config_portal_start(), call
 *     axk_captive_portal_start_services()
 *   - In axk_config_portal.c axk_config_portal_stop(), call
 *     axk_captive_portal_stop_services()
 */

#include "axk_captive_portal.h"
#include "axk_captive_dns.h"
#include "axk_platform.h"

#include <string.h>
#include <stdio.h>

#include "lwip/api.h"

/* ==================== Known Probe Detection ==================== */

/**
 * @brief Known captive probe paths (null-separated, double-null terminated)
 *
 * Apple:  /hotspot-detect.html, /library/test/success.html
 * Google: /generate_204
 * Windows: /connecttest.txt, /ncsi.txt
 * Samsung: /success.txt
 * Microsoft: /fwlink/
 */
static const char s_probe_paths[] = CAPTIVE_PROBE_PATHS;
#define PROBE_LIST_END (s_probe_paths + sizeof(s_probe_paths))

bool axk_captive_portal_is_probe(const char *uri)
{
    if (!uri || uri[0] != '/') {
        return false;
    }

    const char *p = s_probe_paths;
    while (p < PROBE_LIST_END) {
        if (p[0] == '\0') {
            p++;
            continue;
        }
        if (strcmp(uri, p) == 0) {
            return true;
        }
        p += strlen(p) + 1;
    }
    return false;
}

/* ==================== HTTP 302 Redirect ==================== */

int axk_captive_portal_redirect(void *client, const char *location)
{
    if (!client || !location) {
        return -1;
    }

    struct netconn *nc = (struct netconn *)client;
    char header[384];
    int len;

    len = snprintf(header, sizeof(header),
        "HTTP/1.1 302 Found\r\n"
        "Location: %s\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-cache, no-store, must-revalidate\r\n"
        "Pragma: no-cache\r\n"
        "\r\n",
        location);

    if (len <= 0 || len >= (int)sizeof(header)) {
        return -1;
    }

    err_t err = netconn_write(nc, header, len, NETCONN_COPY);
    if (err != ERR_OK) {
        AXK_LOG_DEBUG("[captive_portal] 302 redirect write err=%d\\r\\n", err);
        return -1;
    }

    return 0;
}

/* ==================== Service Lifecycle ==================== */

int axk_captive_portal_start_services(void)
{
    int ret = 0;

    /* Start DNS hijack */
    if (axk_captive_dns_start() != 0) {
        AXK_LOG_ERROR("[captive_portal] DNS hijack start FAIL\\r\\n");
        /* Continue — still usable without DNS */
        ret = -1;
    }

    /* DHCP option 114 is auto-enabled via DHCP_CAPTIVE_PORTAL_URL
     * compile-time define in dhcp_server_raw.c.
     * No runtime action needed — the DHCP server is started by
     * wifi_mgmr_ap_start() -> net_al_dhcpd_start().
     */

    AXK_LOG_INFO("[captive_portal] services started (dns=%s, dhcp_opt114=%s)\\r\\n",
                 axk_captive_dns_is_running() ? "OK" : "FAIL",
#if defined(DHCP_CAPTIVE_PORTAL_URL)
                 "enabled"
#else
                 "disabled"
#endif
    );

    return ret;
}

void axk_captive_portal_stop_services(void)
{
    axk_captive_dns_stop();
    AXK_LOG_INFO("[captive_portal] services stopped\\r\\n");
}
