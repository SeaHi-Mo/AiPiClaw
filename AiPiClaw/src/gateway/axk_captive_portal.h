/**
 * @file axk_captive_portal.h
 * @brief Captive Portal redirect module - HTTP 302 + integration API
 * @version 1.0
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note Provides the integration glue for the Captive Portal system:
 *       - HTTP 302 redirect for non-API paths
 *       - Combined start/stop for DNS hijack + DHCP option 114
 *       - Known captive detection domains (Apple/Android/Windows)
 *
 * Usage:
 *   1. Call axk_captive_portal_redirect() from the 404 handler in
 *      axk_config_portal.c to redirect unknown paths to /
 *   2. Call axk_captive_portal_start() / stop() from portal_task
 *      to manage DNS hijack lifecycle
 *   3. DHCP option 114 is built into dhcp_server_raw.c — define
 *      DHCP_CAPTIVE_PORTAL_URL in the project config or portal module
 *      to enable it automatically when the AP DHCP server starts.
 */

#ifndef __AXK_CAPTIVE_PORTAL_H
#define __AXK_CAPTIVE_PORTAL_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Captive Portal known probe paths
 *
 * These are the paths that iOS, Android, and Windows devices probe
 * when connecting to a new WiFi network to detect Captive Portal.
 * We redirect all of them (and anything else not matching our API
 * routes) to the SPA config page.
 */
#define CAPTIVE_PROBE_PATHS \
    "/generate_204\0" \
    "/hotspot-detect.html\0" \
    "/library/test/success.html\0" \
    "/connecttest.txt\0" \
    "/ncsi.txt\0" \
    "/success.txt\0" \
    "/fwlink/\0" \
    "/redirect"

/**
 * @brief Check if a URI is a known captive probe path
 *
 * @param uri The request URI (e.g. "/generate_204")
 * @return true if this is a known captive probe
 */
bool axk_captive_portal_is_probe(const char *uri);

/**
 * @brief Send a 302 redirect response via lwIP netconn
 *
 * @param client The netconn client connection
 * @param location The redirect target (e.g. "/")
 * @return 0 on success, -1 on failure
 */
int axk_captive_portal_redirect(void *client, const char *location);

/**
 * @brief Start all captive portal services
 *
 * Starts:
 *   - DNS hijack (RAW socket, UDP:53 → 192.168.4.1)
 *
 * DHCP option 114 is handled separately — define DHCP_CAPTIVE_PORTAL_URL
 * at build time to enable it. The DHCP server is already running as part
 * of wifi_mgmr_ap_start().
 *
 * @return 0 on success, -1 on failure
 */
int axk_captive_portal_start_services(void);

/**
 * @brief Stop all captive portal services
 */
void axk_captive_portal_stop_services(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_CAPTIVE_PORTAL_H */
