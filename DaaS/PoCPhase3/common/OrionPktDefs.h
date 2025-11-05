/* OrionPktDefs.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _ORION_PKTDEFS_H
#define _ORION_PKTDEFS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "StdPktHdrs.h"

#define  PKTMTU_MAX  1500  // MAX MTU size, hardcoded for now instead using rte_eth_dev_get_mtu().

/* Differentiated Service Code Point (DSCP), RFC2474/8436. Values defined by RFC4594.
 * Pool#1(xxxxx0): Standards action, Pool#2(xxxx11): Experimental or Local Use, Pool#3(xxxx01): Standards Action
 * P4 and TM applications check the Orion DSCP value to determine if INT payload insertion requested by the source.
 */
enum OrionDscp_e
{
  DSCP_IANA_CS0    = 0x00,  // CS0 Standard Undifferentiated Service, pool#1
  DSCP_IANA_CS1    = 0x08,  // CS1 Low-Priority Data, pool#1
  DSCP_IANA_CS2    = 0x10,  // CS2 OAM, pool#1
  DSCP_IANA_AF11   = 0x08,  // AF11 High-Throughput Data, pool#3
  DSCP_IANA_AF12   = 0x0c,  // AF12 High-Throughput Data, pool#3
  DSCP_IANA_AF13   = 0x0e,  // AF13 High-Throughput Data, pool#3
  DSCP_IANA_AF21   = 0x12,  // AF21 Lower-Latency Data, pool#3
  DSCP_IANA_AF22   = 0x14,  // AF22 Lower-Latency Data, pool#3
  DSCP_IANA_AF23   = 0x16,  // AF23 Lower-Latency Data, pool#3
  DSCP_IANA_CS3    = 0x18,  // CS3 Broadcast Video, pool#1
  DSCP_IANA_CS4    = 0x20,  // CS4 Real Time Interactive, pool#1
  DSCP_IANA_CS5    = 0x28,  // CS5 Signaling, pool#1
  DSCP_IANA_CS6    = 0xc0,  // CS6 Network Control, pool#1
  DSCP_P4INT_MDINT = 0x17,  // P4-INT v2.0 eMbedded Data, pool#2
  DSCP_ORION_MDINT = 0x27,  // Orion INT eMbedded Data, pool#2
  DSCP_ORION_TM    = 0x2b,  // Orion TM-INT, pool#2
};
#define DSCP_TO_TOS(_dscp)    ((_dscp & 0x3f) << 2)
#define DSCP_ECN_TO_TOS(_dscp,_ecn)  (((_dscp & 0x3f) << 2) | (_ecn & 0x3))
#define TOS_TO_DSCP(_tos)    ((_tos  & 0xfc) >> 2)

#define UDPPORT_ORION_P4INT    0x1234  // P4-INT spec v2.0 does not designate a value, referred as INT_TBD in 5.6.2
#define UDPPORT_ORION_TMINT    0x1235  // DPDK TM INT
#define UDPPORT_ORION_TMSCHEDINFO  0x1236  // DPDK TM SchedInfo
#define UDPPORT_ORION_TMSYNC    0x1237  // DPDK TM SYNC

enum {
  PKTTYPE_UNKNOWN = 0,
  PKTTYPE_GBS,
  PKTTYPE_EBS,
  PKTTYPE_SYNC,
  PKTTYPE_SCHEDINFO,
  PKTTYPE_OTHER
};

enum {
  INTTYPE_UNKNOWN   = PKTTYPE_UNKNOWN,
  INTTYPE_GBS       = PKTTYPE_GBS,
  INTTYPE_EBS       = PKTTYPE_EBS,
  INTTYPE_SYNC      = PKTTYPE_SYNC,
  INTTYPE_SCHEDINFO = PKTTYPE_SCHEDINFO
};

enum {
  TLV_TYPE_UNKNOWN   = 0,
  TLV_TYPE_VRFINT    = 1,
  TLV_TYPE_TMINT     = 2,
  TLV_TYPE_SYNC      = 3,
  TLV_TYPE_SCHEDINFO = 4,
  TLV_TYPE_OTHER     = 5,
  TLV_TYPE_LAST      = 255
};

enum {
  PKTACTION_UNKNOWN,
  PKTACTION_SCHED_START,
  PKTACTION_SCHED_GBSUPDT,
  PKTACTION_SCHED_SYNCUPDT,
  PKTACTION_SCHED_SYNCTEST,
  PKTACTION_SCHED_INFOUPDT,
  PKTACTION_SCHED_END,
  PKTACTION_OTHER
};

static inline uint16_t get_vlanhdr_offset(void)      { return sizeof(EtherHdr); }  // Assume pkt is vlan tagged! 
static inline uint16_t get_ipv4hdr_offset(bool vlan) { uint16_t hdrLen=sizeof(EtherHdr); return (!vlan) ? hdrLen : hdrLen+sizeof(VlanHdr); }
static inline uint16_t get_udphdr_offset(bool vlan)  { uint16_t hdrLen=sizeof(EtherHdr)+sizeof(Ipv4Hdr); return (!vlan) ? hdrLen : hdrLen+sizeof(VlanHdr); }
static inline uint16_t get_udpdata_offset(bool vlan) { uint16_t hdrLen=sizeof(EtherHdr)+sizeof(Ipv4Hdr)+sizeof(UdpHdr); return (!vlan) ? hdrLen : hdrLen+sizeof(VlanHdr); }

static inline VlanHdr* get_vlanhdr_ptr(char *pkt)             { return (VlanHdr*) (pkt + get_vlanhdr_offset()); }
static inline Ipv4Hdr* get_ipv4hdr_ptr(char *pkt, bool vlan)  { return (Ipv4Hdr*) (pkt + get_ipv4hdr_offset(vlan)); }
static inline UdpHdr*  get_udphdr_ptr(char *pkt, bool vlan)   { return (UdpHdr*)  (pkt + get_udphdr_offset(vlan));  }
static inline char*    get_udpdata_ptr(char *pkt, bool vlan)  { return (char*)    (pkt + get_udpdata_offset(vlan)); }

static inline uint16_t get_ipv4len(uint16_t pktsz, bool vlan) { return pktsz - get_ipv4hdr_offset(vlan); }
static inline uint16_t get_udplen(uint16_t pktsz, bool vlan)  { return pktsz - get_udphdr_offset(vlan);  }

static inline bool is_vlan_pkt(char *pkt) { return (((EtherHdr*)pkt)->ether_type==rte_cpu_to_be_16(RTE_ETHER_TYPE_VLAN)) ? true : false; }

static inline uint16_t
get_udplen_from_pktsize(uint16_t pktsz, bool vlan)
{
  uint16_t hdrLen = sizeof(EtherHdr) + sizeof(Ipv4Hdr);
  if (vlan)
    hdrLen += sizeof(VlanHdr);
  return pktsz - hdrLen;
}

static inline uint16_t get_l4data_offset(char *pkt, bool vlan)
{
  uint16_t l2l3HdrSize = sizeof(EtherHdr) + sizeof(Ipv4Hdr);
  l2l3HdrSize += (vlan==true) ? sizeof(VlanHdr) : 0;
  Ipv4Hdr *iphdr = get_ipv4hdr_ptr(pkt,vlan);
  if (iphdr->next_proto_id == IPPROTO_UDP)
    return (uint16_t) (l2l3HdrSize + sizeof(UdpHdr));
  else if (iphdr->next_proto_id == IPPROTO_TCP)
    return (uint16_t) (l2l3HdrSize + sizeof(TcpHdr));
  else
    rte_panic(" get_l4data_offset(): unsupported protocol %hhu\n", iphdr->next_proto_id);
}
static inline char* get_l4data_ptr(char *pkt, bool vlan) { return (char*) (pkt + get_l4data_offset(pkt, vlan)); }


#ifdef __cplusplus
}
#endif

#endif /* _ORION_PKTHDEF_H */
