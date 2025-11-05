/* OrionP4Int.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _ORION_P4INT_H
#define _ORION_P4INT_H

#define P4INT_TYPE_NOINT  0  // Covering the case where no INT header is present
#define P4INT_TYPE_INTMD  1  // See tna_orion_exp1.p4, hdr.shimHdr.type
#define P4INT_NPT_PRESERVE_L4  2  // See tna_orion_exp1.p4, hdr.shimHdr.npt
#define P4INT_MD_VERSION_2  2  // See tna_orion_exp1.p4, hdr.mdHdr.version
#define P4INT_MD_VERSION_TM1  3  // For tna_orion_tm1 TM experiments
#define P4INT_MD_VERSION  P4INT_MD_VERSION_2  // OBSOLETED
#define  P4INT_MD_INSTRBITMAP  0xcc00
#define P4INT_REMHOPCNT_MAX  8  // MUST match initialized hdr.mdHdr.remHopCnt, used only for sanity check

#ifdef __cplusplus
extern "C" {
#endif

/* P4 INT Shim Header for INT-over-UDP, see P4-INT spec v2.0 Section 5.6.2. This is a modified TLV format.
 * Notes:
 *  1. Modified TLV format:
 *     a. type field is 4 bits with additional npt field. Together acts as a combined type!
 *     b. the lenght field is in 4-byte word, not including this header!!
 *     c. addition npt dependent field, Orion usage has dstPort per P4-INT.
 */
typedef struct P4IntShimHeader_h {
        // Nibbles in littel endian order for x86. m.s. nibble last!!
    uint8_t  rsvd    : 2;
    uint8_t  npt     : 2;  // next protocol type, Orion uses npt=2 to preserve original UDP header after INT stack
    uint8_t  type    : 4;  // INT-MD (1) header type for hop-by-hop stacked INT metadata, see 3.1 and 5.1
    uint8_t  length;    // For use by non-INT switch to skip over insert INT header to locate orginal UDP header and payload
    uint16_t dstPort;    // spec allow port, ipProto, or dscp. For npt=2, the 2nd byte carries original ipProto
}  __attribute__((__packed__)) P4IntShimHeader;

/* P4 INT-MD MetaData Header for hop-by-hop stack INT, see P4 Int v2.0 Section 5.7
 * Notes:
 *  1. Tofino supports many additional intrinsic metadata for TM and egress status, see definitions in
 *     egress_intrinsic_metadata_t and ingress_intrinsic_metadata_for_tm_t by drivers_test.p4pp.
 *  2. The domain specific parameters are not supported by Orion. We can used these to post pipeline debug information.
 *  3. header size must be 4-byte aligned
 *  4. Change 02/17/22 - replaced 2 16 but domain specific parameters with seqno to detect dropped packets 
 *     TM manages seqno 
 */
typedef struct P4IntMDMdHeader_h
{
        // Nibbles in littel endian order for x86. m.s. nibble last!!
    uint8_t rsvd1        : 1;
    uint8_t M            : 1;  // MTU exceeded
    uint8_t E            : 1;  // Max Hop Count Exceeded
    uint8_t D            : 1;  // discard copy/clone
    uint8_t version      : 4;  // 2 for P4-INT spec v2.0
    uint8_t rsvd2;
    uint8_t hopML5bits;    // Per-hop metadata length, in 4-byte words, l.s. 5 bits
    uint8_t  remHopCnt;    // remaining hop count allowed
    uint16_t instrBitmap;  // see P4-INT spec v2.0 p.20. See above Note1!
    uint16_t dsId;    // domain specific id, not supported
    uint32_t seqno;
    // uint16_t dsInstr;    // domain specific instructions, not supported
    // uint16_t dsFlags;    // domain specific flags, not supported
    // Orion omitted the next 2 parameters (dsInstr and dsFlags)
    // The value of instrBitmap is hardcoded to 0xcc00 for the support INT data (switchId,igPort,egPort,igTs,egTs)

    // List of P4IntMDMdV2Stack_h or P4IntMDMdTM1Stack_h, per version type, follows, one per switch.
    // Orion terminates INT payload with preserved original UDP Header
}  __attribute__((__packed__)) P4IntMDMdHeader;

/* P4 INT Metadata Stack. Orion hardcoded the supported fields w/o using the P4IntMDMdHeader_h:instrBitmap
 * P4IntMDMdV2Stack_h is little-endian version of P4's "header OrionMDMdStack_h"
 * Notes:
 *  1. vrf id is aka switch id
 *  2. The igPort and egPort are 8 bits instead 16 bits defined by P4-INT spec v2.0
 *  3. The igTs and egTs are size 48 bits provided by Tofino1, they are 64 bits by P4-INT spec v2.0
 *  4. We may want to add qid, qdepth and other info from tofino intrinsic metadata, see P4IntMDMdHeader_h notes.
 *  5. header size must be 4-byte aligned
 *  6. The placement order of igTs,igPort,egPort,egTs are for ease of 64-bit aligned access
 */
typedef struct P4IntMDMdV2Stack_h
{
    uint64_t vrf    : 8;  // aka switchId
    uint64_t pad1   : 8;
    uint64_t igTs   : 48;  // ingress_mac_timestamp, see tna_timestamp.p4 as example of Tofino timestamping
    uint64_t igPort : 8;  // bit<8> instead bits per P4 INT v2.0
    uint64_t egPort : 8;
    uint64_t egTs   : 48;  // egress_global_timestamp, bit<48> instead bit<64> per P4 INT v2.0
}  __attribute__((__packed__)) P4IntMDMdV2Stack;

typedef P4IntMDMdV2Stack P4IntMDMdStack;  // Backward compatibility with ta5

/* P4 INT Metadata Stack for TM Experiments with tna_orion_tm1.
 * P4IntMDMdTM1Stack_h is little-endian version of P4's "header OrionMDMdTM1Stack_h"
 */
typedef struct P4IntMDMdTM1Stack_h
{
    uint64_t vrf    : 8;  // aka switchId
    uint64_t pad1   : 8;
    uint64_t igTs   : 48;  // ingress_mac_timestamp, see tna_timestamp.p4 as example of Tofino timestamping
    uint64_t igPort : 8;  // bit<8> instead bits per P4 INT v2.0
    uint64_t egPort : 8;
    uint64_t egTs   : 48;  // egress_global_timestamp, bit<48> instead bit<64> per P4 INT v2.0

    uint64_t enqQDepth   : 19;  // egress_intrinsic_metadata_t::enq_qdepth
    uint64_t enqCongStat : 2;  // egress_intrinsic_metadata_t::enq_congest_stat for congestion status
    uint64_t pad3        : 6;
    uint64_t enqQid      : 5;
    uint64_t deqQDepth   : 19;  // egress_intrinsic_metadata_t::deq_qdepth
    uint64_t deqCongStat : 2;  // egress_intrinsic_metadata_t::deq_congest_stat for congestion status
    uint64_t pad4        : 11;
}  __attribute__((__packed__)) P4IntMDMdTM1Stack;

#ifdef __cplusplus
}
#endif

#endif /* _ORION_P4INT_H */
