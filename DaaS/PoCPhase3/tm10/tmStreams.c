/* tmStreams.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"

int
StreamPktInit(uint8_t confId, uint8_t sid)
{
  SchedConf *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  IntfConf *ic = &intfConf[sid];

  // For all streams...

  for (int sIdx=1; sIdx <= sc->numStreams; sIdx++)
  {
    StreamCfg *sCfg = &sc->streamCfg[confId][sIdx];

    // Set stream type (bandwidth dominate vs latency dominate) based on latency value and rate

    float latRate = 2 * sc->maxPktSize * 8 / sCfg->latency;
    if (sCfg->rate > latRate)
      sCfg->dominance = STREAM_TYPE_BW_DOMINATE;
    else 
      sCfg->dominance = STREAM_TYPE_LAT_DOMINIATE;

    // Initialize pkt content 
  
    #define IPV4_ADDR(d)(((d[0] & 0xff) << 24) | ((d[1] & 0xff) << 16) | ((d[2] & 0xff) << 8) | (d[3] & 0xff))

    // Define outer headers, inner headers obsoleted by tm4
    EtherHdr *o_eth_hdr;
    Ipv4Hdr  *o_ipv4_hdr;

    if (sCfg->pktsize < TELEMETRY_DATA_LEN)
      rte_exit(EXIT_FAILURE, "ERROR: Configured packet size %u too small to contain telemetry data %u\n",
               sCfg->pktsize, TELEMETRY_DATA_LEN);

    uint16_t spPktsize = sCfg->pktsize + sizeof(EtherHdr) + sizeof(Ipv4Hdr) - TELEMETRY_DATA_LEN;
    uint16_t spSrcPort = sCfg->streamId;
    uint16_t spVlanId  = sCfg->vlanId;

    if (ic->vlanTag)
    {
      spPktsize += sizeof(VlanHdr);
    }

    switch (sCfg->protocol)
    {
      case IPPROTO_UDP:
        spPktsize += sizeof(UdpHdr);
        break;
      case IPPROTO_TCP:
        spPktsize += sizeof(TcpHdr);
        break;
      default: 
        rte_exit(EXIT_FAILURE, "ERROR: Unexpected protocol type %hhu for stream#%hhu\n", 
                 sCfg->protocol, sCfg->streamId);
    }
    printf("sIdx %u, streamId %u, pktsize %u (%u+%lu+%lu+%lu+%lu-%u), srcPort %u, vlanId %u\n", 
             sIdx, sCfg->streamId, 
             spPktsize, sCfg->pktsize, sizeof(UdpHdr), sizeof(Ipv4Hdr), sizeof(VlanHdr), sizeof(EtherHdr), TELEMETRY_DATA_LEN,
             spSrcPort, spVlanId);

    if (spPktsize > PKTMTU_MAX)
                  rte_panic(" Error: pktlen plus headers (%u) exceeded MTU size %u\n", 
                              spPktsize, PKTMTU_MAX);

    // Update stream cfg - sacrificing multiple ports and using it for extra stream configuration
    //struct rte_mbuf **pmbuf = &ss->streamPktMbuf[sc->txPort][sIdx];
    struct rte_mbuf **pmbuf = &ss->streamPktMbuf[confId][sIdx];
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(pktmbufPool);
    *pmbuf = mbuf;
    mbuf->hash.usr = sIdx;  // cache stream index in packet for runtime packet touches (e.g. seqno)

    // Sanity check
    if (sIdx > sc->numStreams)
      rte_panic(" PktTemplateInit: Bad stream index %d as >%d\n", (int)sIdx, (int)sc->numStreams);
    if (mbuf == NULL)
      rte_panic(" PktTemplateInit: mbuf allocation failed for stream index %d\n", sIdx);

    /* Outer Header Initializations (no inner headers defined!) */
    o_eth_hdr = rte_pktmbuf_mtod(mbuf, EtherHdr*);
    o_eth_hdr->dst_addr = ic->schedPath.dstAddr;
    o_eth_hdr->src_addr = ic->schedPath.srcAddr;

    if (!ic->vlanTag)
    {
      o_eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
      o_ipv4_hdr = (Ipv4Hdr*) ((char*)o_eth_hdr + sizeof(EtherHdr));
    }
    else
    {
      VlanHdr  *o_vlan_hdr;
      o_eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN);  // endian conversion similar as htons()
      o_vlan_hdr = (VlanHdr*) ((char*)o_eth_hdr + sizeof(EtherHdr));
      o_vlan_hdr->tci = rte_cpu_to_be_16(GET_VLANTCI(sCfg->vlanPri, 0, spVlanId));
      o_vlan_hdr->type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
      o_ipv4_hdr = (Ipv4Hdr*) ((char*)o_vlan_hdr + sizeof(VlanHdr));
    }

    o_ipv4_hdr->version_ihl = 0x45;
    o_ipv4_hdr->type_of_service = DSCP_ECN_TO_TOS(ic->dscp, ic->ecn);
    o_ipv4_hdr->total_length = rte_cpu_to_be_16(get_ipv4len(spPktsize, ic->vlanTag));
    o_ipv4_hdr->packet_id = 0;  // may be touched later if tgRc.updateSeqNo=true
    o_ipv4_hdr->fragment_offset = 0;

    o_ipv4_hdr->time_to_live = sCfg->ttl;  // Configurable per stream ttl with cfgfile

    o_ipv4_hdr->next_proto_id = sCfg->protocol;

    o_ipv4_hdr->src_addr = rte_cpu_to_be_32(IPV4_ADDR(sCfg->srcIP));
    o_ipv4_hdr->dst_addr = rte_cpu_to_be_32(IPV4_ADDR(sCfg->dstIP));
    o_ipv4_hdr->hdr_checksum = (uint16_t) 0; // init for hw offload or subsequent sw computation.
    if (!ic->hwChksumOffload)
      o_ipv4_hdr->hdr_checksum = rte_ipv4_cksum((const struct rte_ipv4_hdr *)o_ipv4_hdr);
    mbuf->ol_flags = RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM;

    char *nextHdr = (char *)(o_ipv4_hdr) + sizeof(Ipv4Hdr);

    if (sCfg->protocol == IPPROTO_UDP)
    {
      UdpHdr *o_udp_hdr = (UdpHdr*) nextHdr;
      o_udp_hdr->src_port = rte_cpu_to_be_16(spSrcPort);  /* NOTE: P4 exp2c uses 5-tuple (including srcPort) to compute wecmpHash */
      o_udp_hdr->dst_port = rte_cpu_to_be_16(ic->dstPort);
      uint16_t pktsize = spPktsize;

      o_udp_hdr->dgram_len = rte_cpu_to_be_16(get_udplen(pktsize, ic->vlanTag));
      o_udp_hdr->dgram_cksum = 0;  // init for hw offload or subsequent sw computation.
      mbuf->ol_flags |= RTE_MBUF_F_TX_UDP_CKSUM;
      if (!ic->hwChksumOffload)
        o_udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum((const struct rte_ipv4_hdr *)o_ipv4_hdr, o_udp_hdr);
      //nextHdr = (char *)(o_udp_hdr) + sizeof(UdpHdr);
    }
    else if (sCfg->protocol == IPPROTO_TCP)
    {
      TcpHdr *o_tcp_hdr = (TcpHdr*) nextHdr;
      o_tcp_hdr->src_port = rte_cpu_to_be_16(spSrcPort);  /* NOTE: P4 exp2c uses 5-tuple (including srcPort) to compute wecmpHash */
      o_tcp_hdr->dst_port = rte_cpu_to_be_16(ic->dstPort);
      o_tcp_hdr->sent_seq = 0;
      o_tcp_hdr->data_off = 5 << 4;  // 5*4=20bytes, no options
      o_tcp_hdr->cksum = 0;  // init for hw offload or subsequent sw computation.
      mbuf->ol_flags |= RTE_MBUF_F_TX_TCP_CKSUM;
      if (!ic->hwChksumOffload)
        o_tcp_hdr->cksum = rte_ipv4_udptcp_cksum((const struct rte_ipv4_hdr *)o_ipv4_hdr, o_tcp_hdr);
      //nextHdr = (char *)(o_tcp_hdr) + sizeof(TcpHdr);
    }
    else
    {
      rte_exit(EXIT_FAILURE, "ERROR: Unexpected protocol type %hhu for stream#%hhu\n", sCfg->protocol, sCfg->streamId);
    }

    mbuf->data_len = spPktsize;
    mbuf->pkt_len  = spPktsize;

    //rte_pktmbuf_dump(stdout, mbuf, 1000);
  }

  return 0;
}

