/**
 * @file axk_captive_dns.h
 * @brief Captive Portal DNS hijack module
 * @version 1.0
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note Hijacks all UDP:53 DNS queries on the SoftAP interface and
 *       returns 192.168.4.1 (the device itself) for ALL domain names.
 *       This ensures that when a mobile device connects to the SoftAP,
 *       any HTTP request it makes is redirected to the device's HTTP
 *       server, triggering the Captive Portal popup on iOS/Android.
 *
 *       Implementation: lwIP RAW socket (protocol IP_PROTO_UDP) bound
 *       to the AP netif IP 192.168.4.1. For each received DNS query,
 *       we parse the Transaction ID and QNAME, and synthesise a minimal
 *       DNS response (A record, TTL=60s) pointing back to 192.168.4.1.
 *
 *       The response is a flat buffer ~80 bytes: no compression needed,
 *       no recursion, no additional/authority sections.
 */

#ifndef __AXK_CAPTIVE_DNS_H
#define __AXK_CAPTIVE_DNS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the DNS hijack service
 *
 * Creates an lwIP RAW PCB bound to UDP protocol, registered on the
 * SoftAP IP (192.168.4.1). All incoming DNS queries (UDP dst port 53)
 * received on the AP netif will be intercepted and answered with a
 * forged response pointing all domains to 192.168.4.1.
 *
 * The RAW callback runs in tcpip_thread context (locked).
 *
 * @return 0 on success, -1 on failure
 */
int axk_captive_dns_start(void);

/**
 * @brief Stop the DNS hijack service
 *
 * Removes the RAW PCB and frees resources. After this call, DNS
 * traffic on the SoftAP flows normally (or gets no response).
 */
void axk_captive_dns_stop(void);

/**
 * @brief Check if the DNS hijack service is running
 *
 * @return true if running, false otherwise
 */
bool axk_captive_dns_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* __AXK_CAPTIVE_DNS_H */
