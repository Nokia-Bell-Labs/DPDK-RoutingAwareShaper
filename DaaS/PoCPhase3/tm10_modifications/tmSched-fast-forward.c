/*
** tmSched.c  (FORWARD-ONLY FAST PATH)
**
**  Keep HW VLAN insertion (Option A)
**  Remove enqueue/dequeue rings + scheduler
**  Single tight loop: rx_burst -> (srcIP lookup) -> MAC rewrite + HW VLAN insert -> tx_burst
**
**              © 2026 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "tmDefs.h"
#include "parserLib.h"
#include "../common/OrionLog.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>

#include <rte_branch_prediction.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_ip.h>
#include <rte_mbuf.h>
// #include <rte_vlan.h>
#include <rte_arp.h>


/* ---------- Jumbo settings (simple ON/OFF switch) ---------- */
#ifndef TM_ENABLE_JUMBO
#define TM_ENABLE_JUMBO 1        /* set to 0 to go back to 1500B */
#endif

#if TM_ENABLE_JUMBO
  /* 9000B MTU needs ~ 9.6KB L2 frame room. CRC is stripped, but we add slack. */
  #define TM_JUMBO_MTU        9000
  #define TM_MAX_RX_PKT_LEN   9600   /* safe for 9K MTU + VLAN/L2 */
  /* mbuf buf_size includes headroom */
  #define TM_MBUF_BUF_SIZE   (RTE_PKTMBUF_HEADROOM + TM_MAX_RX_PKT_LEN + 128)
#else
  #define TM_MBUF_BUF_SIZE    RTE_MBUF_DEFAULT_BUF_SIZE
#endif



/* ------------------------------------------------------------------------------------
 * Keep your existing globals / externs
 * ------------------------------------------------------------------------------------ */
int debugLog = 0;
int errorLog = 1;
int infoLog  = 1;

uint32_t ecn_mark_threshold = 0; /* unused in forward-only build */

uint16_t schedClassifierType;     /* unused in forward-only build */
VlanLookupEntry vlanid_table[4096]; /* unused in forward-only build */

extern IntfConf   intfConf[];
/* runConf/schedConf/schedState/forceQuit are expected from your existing codebase */

/* ------------------------------------------------------------------------------------
 * SrcIP -> policy mapping (kept)
 * ------------------------------------------------------------------------------------ */

static inline uint8_t tm_srcidx_get(const struct rte_mbuf *m)
{
    return (uint8_t)(m->hash.usr & 0xFFu);
}

static inline void tm_srcidx_set(struct rte_mbuf *m, uint8_t idx)
{
    /* keep upper bits free for future use */
    m->hash.usr = (m->hash.usr & 0xFFFFFF00u) | (uint32_t)idx;
}

typedef struct {
    uint32_t src_ip_be;            /* stored in network byte order */
    uint16_t vlan;                 /* 0..4095 (0 means “no override”) */
    uint8_t  have_dst, have_src;
    struct rte_ether_addr dst_mac;
    struct rte_ether_addr src_mac;
} src_vlan_entry_t;

#define SRCMAP_MAX 32
static src_vlan_entry_t g_srcmap[SRCMAP_MAX];
static int  g_srcmap_cnt    = 0;
static int  g_srcmap_loaded = 0;


void mac_address_printf(struct rte_ether_addr *macaddr)
{
  uint8_t *locaddr = (uint8_t *)macaddr;
  printf("%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", locaddr[0], locaddr[1], locaddr[2], locaddr[3], locaddr[4], locaddr[5]);
  return;
}



typedef struct __rte_cache_aligned {
    struct rte_ether_addr dst;
    struct rte_ether_addr src;
    uint16_t tci;   /* host order (pcp<<13 | vid) */
} sid_policy_entry_t;

typedef struct __rte_cache_aligned {
    struct rte_ether_addr def_dst;
    struct rte_ether_addr def_src;
    uint16_t def_vid;        /* 12-bit */
    uint16_t pcp_bits;       /* (pcp<<13) */
    uint16_t def_tci;        /* pcp_bits | def_vid */
    uint8_t  vlan_tag;       /* 0/1 */
} sid_fastconf_t;

static sid_fastconf_t       g_sid_fast[NUM_SCHED_MAX];
static sid_policy_entry_t   g_sid_pol[NUM_SCHED_MAX][SRCMAP_MAX + 1];
static int g_sid_fast_ready = 0;
static int g_sid_pol_ready  = 0;

/* Fast table for 10.0.x.y */
static uint8_t g_srcidx_10_0[65536];
static int g_srcidx_10_0_ready = 0;

/* --- helpers --- */

static int cmp_srcmap_by_srcip(const void *a, const void *b)
{
    const src_vlan_entry_t *ea = (const src_vlan_entry_t *)a;
    const src_vlan_entry_t *eb = (const src_vlan_entry_t *)b;
    if (ea->src_ip_be < eb->src_ip_be) return -1;
    if (ea->src_ip_be > eb->src_ip_be) return  1;
    return 0;
}

static int parse_mac(const char* s, struct rte_ether_addr* ea)
{
    return rte_ether_unformat_addr(s, ea); /* 0 on success */
}

static int load_srcmap_from_file(const char* path)
{
    FILE* f = fopen(path, "r");
    if (!f) { perror("open TM10_SRCMAP"); return -1; }

    char line[256];
    int n = 0;

    while (fgets(line, sizeof(line), f) && n < SRCMAP_MAX) {
        char* p = line;
        while (isspace((unsigned char)*p)) ++p;
        if (*p == '\0' || *p == '#') continue;

        /* tokens: ip vlan [dstmac] [srcmac] */
        char ip_s[64], vlan_s[32], dmac_s[64], smac_s[64];
        ip_s[0]=vlan_s[0]=dmac_s[0]=smac_s[0]='\0';

        int tok = sscanf(p, "%63s %31s %63s %63s", ip_s, vlan_s, dmac_s, smac_s);
        if (tok < 2) continue;

        struct in_addr ina;
        if (inet_pton(AF_INET, ip_s, &ina) != 1) continue;

        long vlan = strtol(vlan_s, NULL, 0);
        if (vlan < 0 || vlan > 4095) vlan = 0;

        g_srcmap[n].src_ip_be = ina.s_addr;                     /* BE */
        g_srcmap[n].vlan      = (uint16_t)(vlan & 0x0FFF);      /* VID only */
        g_srcmap[n].have_dst  = 0;
        g_srcmap[n].have_src  = 0;

        if (tok >= 3 && dmac_s[0] && parse_mac(dmac_s, &g_srcmap[n].dst_mac) == 0)
            g_srcmap[n].have_dst = 1;
        if (tok >= 4 && smac_s[0] && parse_mac(smac_s, &g_srcmap[n].src_mac) == 0)
            g_srcmap[n].have_src = 1;

        n++;
    }
    fclose(f);

    g_srcmap_cnt = n;

    if (g_srcmap_cnt > 1) {
        qsort(g_srcmap, (size_t)g_srcmap_cnt, sizeof(g_srcmap[0]), cmp_srcmap_by_srcip);
    }
    return n;
}

static inline void srcmap_build_fast_10_0(void)
{
    memset(g_srcidx_10_0, 0, sizeof(g_srcidx_10_0));

    for (int i = 0; i < g_srcmap_cnt; i++) {
        uint32_t sip = rte_be_to_cpu_32(g_srcmap[i].src_ip_be); /* 0x0A00XXYY for 10.0.X.Y */
        if ((sip >> 16) == 0x0A00) {
            uint16_t key = (uint16_t)(sip & 0xFFFF);
            g_srcidx_10_0[key] = (uint8_t)(i + 1); /* 1..N */
        }
    }
    g_srcidx_10_0_ready = 1;
}

static void srcmap_init_once(void)
{
    if (g_srcmap_loaded) return;

    const char* path = getenv("TM10_SRCMAP");
    if (path && *path) {
        int r = load_srcmap_from_file(path);
        printf("SRCMAP: loaded %d entries from %s\n", r > 0 ? r : 0, path);
    } else {
        printf("SRCMAP: TM10_SRCMAP not set; using intf.cfg defaults.\n");
    }

    srcmap_build_fast_10_0();
    g_srcmap_loaded = 1;
}

static inline void sid_fastconf_build_once(void)
{
    if (g_sid_fast_ready) return;

    for (int sid = 0; sid < NUM_SCHED_MAX; sid++) {
        const uint16_t vid = (uint16_t)(intfConf[sid].vlanId & 0x0FFF);
        const uint16_t pcp = (uint16_t)(intfConf[sid].vlanPri & 0x7);

        g_sid_fast[sid].def_vid  = vid;
        g_sid_fast[sid].pcp_bits = (uint16_t)(pcp << 13);
        g_sid_fast[sid].def_tci  = (uint16_t)(g_sid_fast[sid].pcp_bits | vid);
        g_sid_fast[sid].vlan_tag = (uint8_t)intfConf[sid].vlanTag;

        rte_ether_addr_copy(&intfConf[sid].schedPath.dstAddr, &g_sid_fast[sid].def_dst);
        rte_ether_addr_copy(&intfConf[sid].schedPath.srcAddr, &g_sid_fast[sid].def_src);
    }

    g_sid_fast_ready = 1;
}

static inline void sid_policy_build_once(void)
{
    if (g_sid_pol_ready) return;

    for (int sid = 0; sid < NUM_SCHED_MAX; sid++) {
        /* idx=0 => defaults */
        g_sid_pol[sid][0].dst = g_sid_fast[sid].def_dst;
        g_sid_pol[sid][0].src = g_sid_fast[sid].def_src;
        g_sid_pol[sid][0].tci = g_sid_fast[sid].def_tci;

        for (int i = 0; i < g_srcmap_cnt; i++) {
            const int idx = i + 1;
            g_sid_pol[sid][idx].dst = g_srcmap[i].have_dst ? g_srcmap[i].dst_mac : g_sid_fast[sid].def_dst;
            g_sid_pol[sid][idx].src = g_srcmap[i].have_src ? g_srcmap[i].src_mac : g_sid_fast[sid].def_src;

            const uint16_t vid = g_srcmap[i].vlan ? g_srcmap[i].vlan : g_sid_fast[sid].def_vid;
            g_sid_pol[sid][idx].tci = (uint16_t)(g_sid_fast[sid].pcp_bits | vid);
        }
    }

    g_sid_pol_ready = 1;
}

static inline __rte_always_inline uint8_t
srcmap_lookup_idx(uint32_t src_ip_be_raw)
{
    /* Fast-path for 10.0.x.y using prebuilt table (no bswap) */
    if (likely(g_srcidx_10_0_ready)) {
        const uint8_t *b = (const uint8_t *)&src_ip_be_raw; /* bytes in network order */
        if (likely(b[0] == 10 && b[1] == 0)) {
            const uint16_t key = (uint16_t)(((uint16_t)b[2] << 8) | b[3]); /* X.Y */
            return g_srcidx_10_0[key]; /* 0 or 1..N */
        }
    }

    /* Fallback: binary search in sorted g_srcmap[] by src_ip_be */
    int lo = 0, hi = g_srcmap_cnt - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) >> 1;
        const uint32_t v = g_srcmap[mid].src_ip_be;

        if (v == src_ip_be_raw) return (uint8_t)(mid + 1);
        if (v <  src_ip_be_raw) lo = mid + 1;
        else                    hi = mid - 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------------------
 * Policy application (MAC rewrite + VLAN)
 *
 * IMPORTANT:
 * - For untagged frames: uses HW VLAN insertion (RTE_MBUF_F_TX_VLAN + vlan_tci).
 * - If a VLAN header is still present in the packet (e.g., RX VLAN strip offload not enabled),
 *   we "retag in place" by updating the VLAN TCI and DO NOT request HW insertion (avoids double tag).
 * ------------------------------------------------------------------------------------ */
static inline __rte_always_inline void
apply_srcip_vlan_mac_policy(struct rte_mbuf *m, uint8_t sid)
{
    const uint8_t idx = tm_srcidx_get(m);
    const sid_policy_entry_t *p =
        (idx <= g_srcmap_cnt) ? &g_sid_pol[sid][idx] : &g_sid_pol[sid][0];

    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    rte_ether_addr_copy(&p->dst, &eth->dst_addr);
    rte_ether_addr_copy(&p->src, &eth->src_addr);

    if (unlikely(!g_sid_fast[sid].vlan_tag)) {
        /* ensure we do not insert on TX */
        m->ol_flags &= ~RTE_MBUF_F_TX_VLAN;
        return;
    }

    /* If packet already carries an 802.1Q header, retag in-place (avoid double-tag). */
    if (unlikely(rte_be_to_cpu_16(eth->ether_type) == RTE_ETHER_TYPE_VLAN)) {
        struct rte_vlan_hdr *vh = (struct rte_vlan_hdr *)(eth + 1);
        vh->vlan_tci = rte_cpu_to_be_16(p->tci); /* write tci into header */
        m->ol_flags &= ~RTE_MBUF_F_TX_VLAN;      /* no HW insert */
        return;
    }

    /* HW insert (Option A) */
    m->ol_flags = (m->ol_flags & ~RTE_MBUF_F_TX_VLAN) | RTE_MBUF_F_TX_VLAN;
    m->vlan_tci = p->tci; /* host order */
}

/* ------------------------------------------------------------------------------------
 * Fast-path parsing: compute srcidx once per packet.
 * - IPv4: use ip->src_addr
 * - ARP:  use arp_sip (so ARP also gets mapped if you need it)
 * - else: idx=0 (defaults)
 * ------------------------------------------------------------------------------------ */
static inline __rte_always_inline void
compute_and_cache_srcidx(struct rte_mbuf *m)
{
    /* clear low 8 bits only (keep any upper metadata you might add later) */
    m->hash.usr &= 0xFFFFFF00u;

    const uint16_t plen = (uint16_t)rte_pktmbuf_data_len(m);
    if (unlikely(plen < sizeof(struct rte_ether_hdr))) {
        tm_srcidx_set(m, 0);
        return;
    }

    struct rte_ether_hdr *eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
    uint16_t etype = rte_be_to_cpu_16(eth->ether_type);

    uint8_t *l3 = (uint8_t *)(eth + 1);

    /* If VLAN header is present in the mbuf data, skip it for L3 parsing */
    if (unlikely(etype == RTE_ETHER_TYPE_VLAN)) {
        if (unlikely(plen < sizeof(struct rte_ether_hdr) + sizeof(struct rte_vlan_hdr))) {
            tm_srcidx_set(m, 0);
            return;
        }
        struct rte_vlan_hdr *vh = (struct rte_vlan_hdr *)l3;
        etype = rte_be_to_cpu_16(vh->eth_proto);
        l3 = (uint8_t *)(vh + 1);
    }

    if (likely(etype == RTE_ETHER_TYPE_IPV4)) {
        if (unlikely((uintptr_t)(l3 + sizeof(struct rte_ipv4_hdr)) >
                     (uintptr_t)rte_pktmbuf_mtod(m, uint8_t *) + plen)) {
            tm_srcidx_set(m, 0);
            return;
        }
        struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)l3;
        if (unlikely((ip->version_ihl >> 4) != 4)) {
            tm_srcidx_set(m, 0);
            return;
        }
        tm_srcidx_set(m, srcmap_lookup_idx(ip->src_addr));
        return;
    }

    if (unlikely(etype == RTE_ETHER_TYPE_ARP)) {
        if (unlikely((uintptr_t)(l3 + sizeof(struct rte_arp_hdr)) >
                     (uintptr_t)rte_pktmbuf_mtod(m, uint8_t *) + plen)) {
            tm_srcidx_set(m, 0);
            return;
        }
        struct rte_arp_hdr *arp = (struct rte_arp_hdr *)l3;
        tm_srcidx_set(m, srcmap_lookup_idx(arp->arp_data.arp_sip));
        return;
    }

    tm_srcidx_set(m, 0);
}

/* ------------------------------------------------------------------------------------
 * Lcore / instance mapping (kept as your original: single sid=0)
 * ------------------------------------------------------------------------------------ */
static unsigned
LcoreIdToSchedId(unsigned lcoreId)
{
    (void)lcoreId;
    /* Your original code always returns 0 (single instance). Keep it. */
    return 0;
}

/* ------------------------------------------------------------------------------------
 * Forward-only tight loop
 * ------------------------------------------------------------------------------------ */
static void
SchedFastFwdThread(unsigned lcoreId)
{
    const unsigned sid = LcoreIdToSchedId(lcoreId);
    SchedConf *sc = &schedConf[sid];

    /* one-time init */
    srcmap_init_once();
    sid_fastconf_build_once();
    sid_policy_build_once();

    /* optional: keep epoch in schedState for any other code that reads it */
    SchedState *ss = &schedState[sid];
    ss->schedId  = sid;
    ss->tscEpoch = rte_rdtsc();

    const uint16_t rxPort = sc->rxPort;
    const uint16_t txPort = sc->txPort;

    const uint16_t rxBurstSize = sc->rxBurstSize ? sc->rxBurstSize : 32;
    struct rte_mbuf *pkts[256] __rte_cache_aligned;
    const uint16_t burst = (rxBurstSize <= (uint16_t)RTE_DIM(pkts)) ? rxBurstSize : (uint16_t)RTE_DIM(pkts);

    const uint16_t txq = runConf.txqId;

    RTE_LOG(INFO, SCHED, "FASTFWD: lcore %u sid %u rxPort %u -> txPort %u (txq=%u)\n",
            lcoreId, sid, rxPort, txPort, txq);

    while (likely(!forceQuit)) {

        /* If you have multiple RX queues, preserve your old “highest queue first” polling */
        int nb_rx = 0;
        uint16_t q = (runConf.rxqNum > 0) ? (uint16_t)(runConf.rxqNum - 1) : 0;
        while (1) {
            nb_rx = (int)rte_eth_rx_burst(rxPort, q, pkts, burst);
            if (nb_rx > 0 || q == 0) break;
            q--;
        }

        if (unlikely(nb_rx <= 0))
            continue;

        /* packet processing */
        for (int i = 0; i < nb_rx; i++) {
            struct rte_mbuf *m = pkts[i];

            /* compute srcidx once */
            compute_and_cache_srcidx(m);

            /* rewrite MAC + VLAN (HW insert) */
            apply_srcip_vlan_mac_policy(m, (uint8_t)sid);
        }

        /* transmit as a burst */
        uint16_t nb_tx = rte_eth_tx_burst(txPort, txq, pkts, (uint16_t)nb_rx);

        /* free unsent */
        if (unlikely(nb_tx < (uint16_t)nb_rx)) {
            for (uint16_t i = nb_tx; i < (uint16_t)nb_rx; i++)
                rte_pktmbuf_free(pkts[i]);
        }
    }

    printf("SchedFastFwdThread() exiting on lcore %u\n", lcoreId);
}

/* ------------------------------------------------------------------------------------
 * Main thread: keep minimal (optional). You can delete if you want.
 * ------------------------------------------------------------------------------------ */
static void
SchedMainThread(unsigned lcoreId)
{
    unsigned sid = LcoreIdToSchedId(lcoreId);
    SchedConf *sc = &schedConf[sid];

    unsigned timerSec = runConf.statsTimerSec;
    if (timerSec == 0) timerSec = 1;

    RTE_LOG(INFO, SCHED, "MAIN: lcore %u stats interval = %u seconds\n", lcoreId, timerSec);

    while (!forceQuit) {
        rte_delay_ms(1000 * timerSec);

        /* keep your existing summary helper if you have it */
        uint64_t drops = 0;
        SummaryEtherPortStatsPrint(sc->txPort, timerSec, &drops);
        (void)drops;
    }

    printf("SchedMainThread() exiting!\n");
}

/* ------------------------------------------------------------------------------------
 * Dispatcher: run only the fast-forward loop on rxCore.
 * - tmCore/txCore do nothing (so you can keep your lcore config unchanged)
 * ------------------------------------------------------------------------------------ */
void
SchedThreadsDispatcher(void)
{
    unsigned lcoreId = rte_lcore_id();
    unsigned sid = LcoreIdToSchedId(lcoreId);
    SchedConf *sc = &schedConf[sid];

    if (lcoreId == sc->rxCore) {
        SchedFastFwdThread(lcoreId);
    } else if (lcoreId == rte_get_main_lcore()) {
        SchedMainThread(lcoreId);
    } else {
        /* keep other lcores idle (or you can rte_pause() in a loop) */
        while (!forceQuit) {
            rte_delay_ms(200);
        }
    }

    printf("sched_thread_dispatcher returning for lcore %u!\n", lcoreId);
}
