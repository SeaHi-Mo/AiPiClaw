/**
 * @file axk_captive_dns.c
 * @brief Captive Portal DNS hijack - RAW socket implementation
 * @version 1.0
 * @date 2026-05-16
 *
 * @copyright Copyright (c) 2026 AI-Thinker
 *
 * @note Implements DNS query interception on lwIP RAW socket.
 *       Minimal DNS response builder — no DNS library needed.
 *
 * DNS Query (incoming):
 *   [12 bytes header]  [QNAME]   [QTYPE 2 bytes]  [QCLASS 2 bytes]
 *
 * DNS Response (we send):
 *   [12 bytes header, ID copied, QR=1, RA=1, RCODE=0]
 *   [QNAME echo - same as query]
 *   [QTYPE/QCLASS echo]
 *   [A record: name=0xc00c (pointer), type=1, class=1, TTL=60, IP=192.168.4.1]
 *
 * Total response ~ 16 (header) + QNAME_len + 4 (QTYPE+QCLASS) + 16 (A record)
 * For typical queries like "captive.apple.com" = ~72 bytes.
 */

#include "axk_captive_dns.h"
#include "axk_platform.h"

#include <string.h>
#include <stdlib.h>

#include "lwip/opt.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include "lwip/ip.h"
#include "lwip/netif.h"
#include "lwip/inet_chksum.h"
#include "lwip/udp.h"

/* ==================== Constants ==================== */

#define DNS_PORT            53
#define DNS_HEADER_LEN      12
#define DNS_TYPE_A          1
#define DNS_CLASS_IN        1
#define DNS_QR_FLAG         0x8000  /* Response bit */
#define DNS_RA_FLAG         0x0080  /* Recursion Available */
#define DNS_OPCODE_STD      0x0000  /* Standard query */
#define DNS_RCODE_NOERROR   0x0000
#define DNS_TTL             60      /* Short TTL so phones re-query often */

/* The IP we redirect all DNS to */
#define CAPTIVE_IP_ADDR     IPADDR4_INIT_BYTES(192, 168, 4, 1)

/* ==================== Internal State ==================== */

static struct raw_pcb *s_dns_pcb = NULL;

/* ==================== DNS Protocol Helpers ==================== */

/**
 * @brief Write a 16-bit big-endian value
 */
static inline void dns_write_u16(uint8_t *buf, uint16_t val)
{
    buf[0] = (uint8_t)(val >> 8);
    buf[1] = (uint8_t)(val & 0xff);
}

/**
 * @brief Write a 32-bit big-endian value
 */
static inline void dns_write_u32(uint8_t *buf, uint32_t val)
{
    buf[0] = (uint8_t)(val >> 24);
    buf[1] = (uint8_t)(val >> 16);
    buf[2] = (uint8_t)(val >> 8);
    buf[3] = (uint8_t)(val & 0xff);
}

/**
 * @brief Read a 16-bit big-endian value
 */
static inline uint16_t dns_read_u16(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

/**
 * @brief Get QNAME length (including all label lengths and final null)
 *
 * DNS QNAME is a series of length-prefixed labels, terminated by a
 * zero-length label (0x00). Returns total bytes including the terminator.
 *
 * Labels are restricted to 63 bytes max (0x3f), and the two high bits
 * (0xc0) indicate a compression pointer — we treat those as EOM.
 */
static int dns_qname_length(const uint8_t *buf, int max_len)
{
    int total = 0;
    while (total < max_len) {
        uint8_t len = buf[total];
        if (len >= 0xc0) {
            /* Compression pointer (2 bytes total) — treat as end */
            total += 2;
            return total;
        }
        total += 1 + len;  /* length byte + label content */
        if (len == 0) {
            return total;  /* Root label terminator */
        }
    }
    return total;  /* truncated */
}

/* ==================== RAW Receive Callback ==================== */

/**
 * @brief lwIP RAW recv callback — intercept UDP:53, forge DNS reply
 *
 * This runs in tcpip_thread context. We parse the incoming DNS query
 * and build a minimal DNS response directing EVERYTHING to 192.168.4.1.
 *
 * @return 1 (packet consumed, we own it), 0 (not consumed)
 */
static uint8_t dns_raw_recv_cb(void *arg, struct raw_pcb *pcb,
                                struct pbuf *p, const ip_addr_t *addr)
{
    (void)arg;
    (void)pcb;

    /* Reject too-small packets */
    if (p->tot_len < (DNS_HEADER_LEN + 5)) {
        AXK_LOG_DEBUG("[captive_dns] packet too small (%d), ignoring\\r\\n", p->tot_len);
        return 0;
    }

    /* Only respond to standard DNS queries on the AP interface */
    struct netif *rx_netif = ip_current_netif();
    if (!rx_netif) {
        return 0;
    }

    /* Check this came from the AP netif (192.168.4.x) */
    const ip4_addr_t *rx_ip = ip_2_ip4(netif_ip4_addr(rx_netif));
    if (rx_ip->addr != CAPTIVE_IP_ADDR.addr) {
        return 0;  /* Not AP netif — let traffic pass */
    }

    /* Parse the query to extract Transaction ID and QNAME */
    uint8_t query_buf[256];
    uint16_t query_len = (p->tot_len > (int)sizeof(query_buf))
                         ? (int)sizeof(query_buf) : p->tot_len;

    /* Copy pbuf chain into flat buffer */
    pbuf_copy_partial(p, query_buf, query_len, 0);

    /* Verify it looks like a DNS query */
    uint16_t flags = dns_read_u16(query_buf + 2);
    if (flags & DNS_QR_FLAG) {
        return 0;  /* Not a query — skip */
    }

    /* Extract Transaction ID */
    uint16_t tx_id = dns_read_u16(query_buf);

    /* Count questions. If zero, ignore. */
    uint16_t qdcount = dns_read_u16(query_buf + 4);
    if (qdcount == 0) {
        return 0;
    }

    /* Locate QNAME start and determine its length */
    uint8_t *qname_start = query_buf + DNS_HEADER_LEN;
    int qname_remain = query_len - DNS_HEADER_LEN;
    if (qname_remain < 5) {
        return 0;
    }

    int qname_len = dns_qname_length(qname_start, qname_remain);
    if (qname_len < 1) {
        return 0;
    }

    /* QTYPE and QCLASS follow QNAME */
    uint8_t *qtype_ptr = qname_start + qname_len;
    if ((qtype_ptr + 4) > (query_buf + query_len)) {
        return 0;
    }

    uint16_t qtype = dns_read_u16(qtype_ptr);
    uint16_t qclass = dns_read_u16(qtype_ptr + 2);

    /* ============ Build Response ============ */
    /* Response size: header(12) + QNAME(qname_len) + QTYPE/QCLASS(4) +
     * DNS_NAME_COMP(2) + TYPE(2) + CLASS(2) + TTL(4) + RDLENGTH(2) + RDATA(4) */
    int resp_hdr_section = DNS_HEADER_LEN + qname_len + 4;
    int resp_ans_section = 2 + 2 + 2 + 4 + 2 + 4; /* ptr + type + class + ttl + rdlen + rdata */
    int resp_total = resp_hdr_section + resp_ans_section;

    uint8_t resp[resp_total];
    memset(resp, 0, resp_total);

    /* --- Header --- */
    dns_write_u16(resp, tx_id);                   /* Transaction ID (copy from query) */
    dns_write_u16(resp + 2,                        /* Flags: QR=1, OPCODE, AA=1, RA=1 */
                  DNS_QR_FLAG | DNS_RA_FLAG | DNS_OPCODE_STD);
    dns_write_u16(resp + 4, 1);                   /* QDCOUNT = 1 (echo question) */
    dns_write_u16(resp + 6, 1);                   /* ANCOUNT = 1 (our answer) */
    dns_write_u16(resp + 8, 0);                   /* NSCOUNT = 0 */
    dns_write_u16(resp + 10, 0);                  /* ARCOUNT = 0 */

    /* --- Question section (echo original QNAME + TYPE + CLASS) --- */
    memcpy(resp + DNS_HEADER_LEN, qname_start, qname_len);
    memcpy(resp + DNS_HEADER_LEN + qname_len, qtype_ptr, 4);

    /* --- Answer section --- */
    uint8_t *ans = resp + resp_hdr_section;

    *(ans++) = 0xc0;                               /* Name compression pointer */
    *(ans++) = DNS_HEADER_LEN;                     /* Points to QNAME start */

    dns_write_u16(ans, DNS_TYPE_A); ans += 2;      /* TYPE = A (1) */
    dns_write_u16(ans, DNS_CLASS_IN); ans += 2;    /* CLASS = IN (1) */
    dns_write_u32((uint32_t *)ans, DNS_TTL); ans += 4; /* TTL */
    dns_write_u16(ans, 4); ans += 2;               /* RDLENGTH = 4 */
    dns_write_u32((uint32_t *)ans, CAPTIVE_IP_ADDR.addr); /* RDATA = 192.168.4.1 */

    /* Build output pbuf */
    struct pbuf *out_p = pbuf_alloc(PBUF_TRANSPORT, resp_total, PBUF_POOL);
    if (out_p == NULL) {
        AXK_LOG_ERROR("[captive_dns] pbuf_alloc failed (%d bytes)\\r\\n", resp_total);
        return 0;
    }

    /* Copy response data */
    pbuf_take(out_p, resp, resp_total);

    /* Send back to source using the source address/port */
    ip_addr_t src_addr;
    ip_addr_copy_from_ip4(src_addr, *ip_2_ip4(addr));
    err_t err = raw_sendto(pcb, out_p, &src_addr);

    if (err != ERR_OK) {
        AXK_LOG_DEBUG("[captive_dns] raw_sendto err=%d\\r\\n", err);
    }

    pbuf_free(out_p);

    return 1;  /* Packet consumed */
}

/* ==================== Public API ==================== */

int axk_captive_dns_start(void)
{
    if (s_dns_pcb != NULL) {
        AXK_LOG_WARN("[captive_dns] already running\\r\\n");
        return 0;
    }

    /* Create RAW PCB for UDP protocol */
    struct raw_pcb *pcb = raw_new(IP_PROTO_UDP);
    if (pcb == NULL) {
        AXK_LOG_ERROR("[captive_dns] raw_new FAIL\\r\\n");
        return -1;
    }

    /* Bind to AP IP: udp:53 — but raw_new binds by protocol only.
     * We filter by AP netif in the recv callback instead of binding to IP. */
    ip_addr_t bind_ip = IPADDR4_INIT_BYTES(192, 168, 4, 1);
    err_t err = raw_bind(pcb, &bind_ip);
    if (err != ERR_OK) {
        AXK_LOG_ERROR("[captive_dns] raw_bind FAIL: %d\\r\\n", err);
        raw_remove(pcb);
        return -1;
    }

    /* Register our recv callback */
    raw_recv(pcb, dns_raw_recv_cb, NULL);

    s_dns_pcb = pcb;

    AXK_LOG_INFO("[captive_dns] DNS hijack started on 192.168.4.1:53\\r\\n");
    return 0;
}

void axk_captive_dns_stop(void)
{
    if (s_dns_pcb == NULL) {
        return;
    }

    raw_remove(s_dns_pcb);
    s_dns_pcb = NULL;

    AXK_LOG_INFO("[captive_dns] DNS hijack stopped\\r\\n");
}

bool axk_captive_dns_is_running(void)
{
    return (s_dns_pcb != NULL);
}
