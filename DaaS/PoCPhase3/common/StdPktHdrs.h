/* StdPktHdrs.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _STD_PKTHDRS_H
#define _STD_PKTHDRS_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Ethernet header: Contains the destination address, source address
 * and frame type. 12 bytes
 */
typedef struct rte_ether_hdr EtherHdr;
#if 0
struct EtherHdr_s {
        struct ether_addr d_addr; /* Destination address. */
        struct ether_addr s_addr; /* Source address. */
        uint16_t ether_type;      /* Frame type. */
} __attribute__((__packed__));
#endif

/**
 * VLAN Header: 4 bytes
 */
//typedef struct rte_vlan_hdr VlanHdr;
typedef struct VlanHdr_s
{
#if 0  // BAD! Not correct endian
  uint16_t vlanPri  : 3;  // vlan priority 0..7, 0 is lowest
  uint16_t vlanDEI  : 1;  // drop eligibility, default is 0 for ineligible
  uint16_t vlanId    : 12;  // vlan id 0..4095
#endif
  uint16_t tci;      // Use GET_VLANTCI()
  uint16_t type;
} __attribute__((__packed__)) VlanHdr;
#define GET_VLANTCI(pri,dei,vid)  (((pri&0x7) << 13) | ((dei&0x1)<<12) | (vid&0xfff))
#define GET_VLANPRI_FROM_TCI(tci)  ((tci >> 13) & 0x7)
#define GET_VLANDEI_FROM_TCI(tci)  ((tci >> 12) & 0x1)
#define GET_VLANID_FROM_TCI(tci)  (tci & 0xfff)

/**
 * IPv4 Header: 20 bytes
 */
struct Ipv4Hdr_s {
        uint8_t  version_ihl;           /* version and header length */
        uint8_t  type_of_service;       /* type of service */
        uint16_t total_length;          /* length of packet */
        uint16_t packet_id;             /* packet ID */
        uint16_t fragment_offset;       /* fragmentation offset */
        uint8_t  time_to_live;          /* time to live */
        uint8_t  next_proto_id;         /* protocol ID */
        uint16_t hdr_checksum;          /* header checksum */
        uint32_t src_addr;              /* source address */
        uint32_t dst_addr;              /* destination address */
} __attribute__((__packed__));
typedef struct Ipv4Hdr_s Ipv4Hdr;

/**
 * UDP Header: 8 bytes
 * BigEndian expected
 */
struct UdpHdr_s {
        uint16_t src_port;    /* UDP source port. */
        uint16_t dst_port;    /* UDP destination port. */
        uint16_t dgram_len;   /* UDP datagram length */
        uint16_t dgram_cksum; /* UDP datagram checksum */
} __attribute__((__packed__));
typedef struct UdpHdr_s UdpHdr;

/**
 * TCP Header: 20 bytes
 * Alias of rte_tcp.h struct rte_tcp_hdr
 * BigEndian expected
 */
struct TcpHdr_s {
  uint16_t src_port; /**< TCP source port. */
  uint16_t dst_port; /**< TCP destination port. */
  uint32_t sent_seq; /**< TX data sequence number. */
  uint32_t recv_ack; /**< RX data acknowledgment sequence number. */
  uint8_t  data_off;   /**< Data offset. */
  uint8_t  tcp_flags;  /**< TCP flags */
  uint16_t rx_win;   /**< RX flow control window. */
  uint16_t cksum;    /**< TCP checksum. */
  uint16_t tcp_urp;  /**< TCP urgent pointer, if any. */
} __attribute__((__packed__));
typedef struct TcpHdr_s TcpHdr;


#ifdef __cplusplus
}
#endif

#endif /* _STD_PKTHDRS_H */
