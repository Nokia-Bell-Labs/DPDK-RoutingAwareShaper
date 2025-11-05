/* OrionTMInt.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _ORION_TMINT_H_
#define _ORION_TMINT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include "OrionDpdk.h"
#include "OrionPktDefs.h"

#define NUM_SCHED_MAX              1
#define NUM_TIMESLOTS_MAX          40000           // Number of time slots in periodic service sequence
#define TM_NUM_RX_RINGS            1024            // Number of shared rx rings (aka scheduler queues) by GBS traffic
#define TM_NUM_TX_RINGS            1               // Hardcoded to 1 by implementation
#define NUM_GBSQUEUES_MAX          TM_NUM_RX_RINGS
#define TM_NUM_CLASSES             8               // Number of traffic classes to analyze
#define TM_CLASS_MASK              0x7             // Mask on vlan ID to get traffic class

#define NUM_SYNCQUEUES_MAX         1               // Max number of Queues for sync pkts, used to timing analysis under idle condition
#define SCHED_CONFIG_FILE_LEN_MAX  64              // Scheduler Config File max len (from --pfc argument), including NULL
#define INTF_CONFIG_FILE_LEN_MAX   64              // Interface Config File max len (from --pfc argument), including NULL

#define QUEUES_PER_BUNDLE_MAX      16              // Max number of queues in a queue bundle

enum RunMode_e
{
  RUNMODE_NORMAL,
  RUNMODE_SYNCTEST
};

enum QueueType_e
{
  QUEUE_TYPE_UNKNOWN,
  QUEUE_TYPE_GBS,
  //QUEUE_TYPE_EBS,
  QUEUE_TYPE_SYNC,
  QUEUE_TYPE_OTHERS
};

enum SchedMode_e
{
  SCHED_MODE_UNKNOWN,
  SCHED_MODE_DCB_Q,
  SCHED_MODE_SRR,
  SCHED_MODE_L2FWD,
  SCHED_MODE_OTHERS
};

enum SchedInit_e
{
  INIT_MASK_STRUCT  = 0x01,
  INIT_MASK_DEQRUNNING  = 0x02,
  INIT_MASK_ENQRUNNING  = 0x10,
  INIT_MASK_TXRUNNING  = 0x20,
  INIT_MASK_L2FWDRUNNING  = 0x40,
};

#define RX_FLOWS_MAX  8
typedef struct RxFlow_s {
  uint16_t flowId;
  uint16_t queueId;                    // mapping of vlanId to dpdk rx queue Id 
  uint16_t vlanTci;                    // vlanId (lower 12 bits) + priority 
  uint16_t vlanTciMask;                // mask (0xffff) for full match
} RxFlow;

typedef struct RunConf_s {
  uint8_t  initMask;                   // enum SchedInit_e
  uint8_t  mode;                       // see RUNMODE_xxx for NORMAL vs SYNCTEST
  bool     interactive;                // Future
  bool     elasticDebit;               // debit for elastic timeslot boundary
  bool     flowIsolate;                // enable if using bifurcation with Linux kernel handling non-matching rxFlow traffic
  bool     promiscuous;                // disable if dstMac unmatched unicast traffic are to be received
  uint32_t linkSpeedMbpsActual;        // actual interface link speed
  uint32_t linkSpeedMbpsConf;          // configured link speed to override actual interface link speed
  uint32_t maxRunPkts;                 // max run duration in number of packets, 0=unlimited
  uint32_t maxRunTimeslots;            // max run duration in number of timeslots, 0=unlimited
  uint16_t rxqNum;                     // number of rx queues
  uint16_t txqNum;                     // number of tx queues
  uint16_t txqId;                      // tx queue id for scheduler tx pkts
  uint16_t rxFlows;
  RxFlow   rxFlow[RX_FLOWS_MAX];
  unsigned statsTimerSec;              // Statistics display timer period in seconds
} __rte_cache_aligned RunConf;

typedef struct BundleConf_s {
  uint16_t bid;                        // Bundle id (for convenience)
  uint16_t numQueues;                  // Number of queues in this bundle; max QUEUES_PER_BUNDLE
  int32_t  numTimeslots;               // Number of timeslots for this bundle, derived from csv cfgfile
  uint32_t schedRate;                  // Scheduling rate of queue
  uint16_t queues[QUEUES_PER_BUNDLE_MAX];  // Map of flow queues to bundle
} BundleConf;

typedef struct IntfConf_s
{
  struct SchedPath_s                   // Scheduler Forwarding Path
  {
    struct rte_ether_addr srcAddr;
    struct rte_ether_addr dstAddr;
  } schedPath;
  struct L2fwdPath_s                   // L2FWD Forwarding Path
  {
    struct rte_ether_addr srcAddr;
    struct rte_ether_addr dstAddr;
  } l2fwdPath;

  // Below are for TM generated pkts (SchedInfo and Sync pkts)
  bool     vlanTag;                    // true if tx pkt shall include vlan header 
  uint8_t  vlanPri;
  uint16_t vlanId;
  uint32_t srcIP;                      // in network format (e.g. big endian)
  uint32_t dstIP;
} IntfConf;

typedef struct SchedConf_s
{
  bool     prevCreditFlag;             // true if use negative credit from previous timeslot  NS3:m_prevCreditFlag
  uint8_t  schedId;                    // index to this instance
  uint8_t  schedMode;                  // SchedMode_e
  uint8_t  rxPort;
  uint8_t  txPort;
  uint8_t  rxCore;
  uint8_t  tmCore;
  uint8_t  txCore;
  uint8_t  l2fCore;                    // simple l2 forwarding for reverse path traffic from txPort to rxPort!
  uint16_t linkSpeedMbps;              // in mbps
  uint16_t timeslotsPerSeq;            // number of timeslots in a scheduling sequence  NS3:m_schedSlots
  uint16_t maxPktSize;                 // maximum size of a packet
  uint16_t rxBurstSize;                // rte_eth_rx_burst() limit
  uint32_t timeslotNsec;               // timeslot duration in nsec
  //uint32_t timeslotUsec;             // timeslot duration in usec
  uint32_t timeslotTsc;                // timeslot duration in tsc ticks  NS3:m_slotDuration
  uint64_t tscHz;
  double   tscHzMeasured;
  uint64_t linkSpeedBpMTsc;            // Link speed in bits per million TSC tics
  uint16_t queuesNum;                  // number of logical queues; in case of bundling, this is the number of bundles
  uint16_t pss[NUM_TIMESLOTS_MAX];     // From csv file, Scheduling sequence of queues assignments indexed by fixed duration timeslot
  BundleConf bundleConf[NUM_GBSQUEUES_MAX]; // Bundle configuration from csv file; number of bundles could equal NUM_QUEUES_MAX, 
                                            // i.e. each flow in its own bundle
  /* Non-Realtime Info */
  char     schedCfgFile[SCHED_CONFIG_FILE_LEN_MAX];
  char     intfCfgFile[INTF_CONFIG_FILE_LEN_MAX];
} __rte_cache_aligned SchedConf;

// OrionMbufUsr is 32-bit representation of rte_mbuf:hash.usr to carry Orion TM metadata with the packet.
typedef union OrionMbufUsr_u
{
  uint32_t  usr;
  struct OrionMbufUsr_s
  {
    uint32_t addTMINT : 1;             // 1 if need to insert INT TLV when dscp=DSCP_ORION_TM
    uint32_t vlan     : 1;             // 1 if is a vlan pkt
    uint32_t rsvd     : 30;            // FUTURE
  } u;
} OrionMbufUsr;


//################################ Scheduler TLV Defintions ###########################################

/* NOTES:
 *  1. The TLV fields (e.g. tlvLength and timestamps) are kept in host endian format to minimum processing overhead.
 */

// Common Scheduler parameters for TM TLVs
// sizeof()=8
typedef struct TMSchedHdr_s
{
  uint8_t   version;  // INT version (version and type used to determine INT structure)
  uint8_t   nodeId;    // endpoint/bridge identifier
  uint8_t   pktType   : 4;  // see PKTTYPE_xxx
  uint8_t   pktAction : 4;  // see PKTACTION_xxx
  uint16_t  streamId;
  uint32_t  seqno;    // sequence number, per logical queue
} __attribute__((__packed__)) TMSchedHdr;

/*
 * GBS/EBS Inband Network Telemetry Payload
 * NOTES: The tragen_tms/pktshdrs.h IntPayload_s has lots scheduler specific internal parameters used during its characterizations!
// sizeof()=4+8+24=36
 */
typedef struct TMGbsTLV_s
{
  uint8_t   tlvType;        // TLV type per TLV_TYPE_xxx
  uint8_t   nextProtocol;
  uint16_t  tlvLength;      // TLV length (used to locate next INT TLV)

  TMSchedHdr  tmsHdr;

  uint16_t  queueId;  // Logical Queue id (also used by trafgen to cache streamId)
  uint8_t   deqStates;
  uint8_t   pad1;

  uint16_t  rxQLen;
  uint16_t  txQLen;
  uint32_t  tscRxLatency;
  uint32_t  tscTxLatency;
  uint64_t  tsRtscTx;

#if 0
  uint64_t  tsphcRx;  // rx timestamp PTP Hardware Clock (phc)

  uint32_t  enqLatency;  // enq latency in tsc
  uint16_t  pathId;    // packed bit mapped of switch id + PFC id (ingress port, egress port, lcores)
  uint8_t    tmColor;
  uint16_t  tmQlen;
#endif
} __attribute__((__packed__)) TMGbsTLV;

typedef struct SynctestInfo_s
{
  bool    defined;
  uint32_t  burstPktsSize;
  uint32_t  burstIntvMsec;
  uint32_t  testDuratonSec;
} __attribute__((__packed__)) SynctestInfo; 

/*
 * Synchronization TLV
 */
typedef struct TMSyncTLV_s
{
  uint8_t    tlvType;        // TLV type per TLV_TYPE_xxx
  uint8_t    pad1;    // for 32bit alignment
  uint16_t  tlvLength;      // TLV length (used to locate next INT TLV)

  TMSchedHdr  tmsHdr;

  uint64_t  tsTscEpoch;  // RTSC epoch. tsc=rtsc+epoch
  uint64_t  tsTscNow;
  struct timespec  tsTodNow;

  uint32_t  burstId;
  SynctestInfo  synctestInfo;  // Used when --synctest is specified
} __attribute__((__packed__)) TMSyncTLV;

typedef struct TMSyncSnapshot_s
{
    bool    valid;
    struct TMSyncData_s
    {
  uint8_t    nodeId;    // TMSchedHdr::nodeId
  uint64_t  tsTscEpoch;  // RTSC epoch. tsc=rtsc+epoch
  uint64_t  tsTscNow;
  struct timespec  tsTodNow;
    } tm;
    struct TaSyncData_s
    {
  uint64_t        tsTscNow;
  struct timespec  tsTodNow;
    } ta;
} TMSyncSnapshot;

typedef struct TMSchedInfoTLV_s
{
  uint8_t    tlvType;        // TLV type per TLV_TYPE_xxx
  uint8_t    pad1;
  uint16_t  tlvLength;      // TLV length, extended to 16 bits for larger SchedInfoTLV!! 

  TMSchedHdr  tmsHdr;

  RunConf    runConf;
  SchedConf  schedConf;
  IntfConf  intfConf;
} __attribute__((__packed__)) TMSchedInfoTLV;

typedef struct TMGenericTLVHdr_s
{
  uint8_t    tlvType;        // TLV type per TLV_TYPE_xxx
  uint8_t    pad1;
  uint16_t  tlvLength;      // TLV length, extended to 16 bits for larger SchedInfoTLV!! 
  uint8_t    data[1];  // type dependent payload data
} __attribute__((__packed__)) TMGenericTLVHdr;

#define UDPGBSPKTSIZE_MIN(vlan)  (get_udphdr_offset(vlan) + sizeof(TMGbsTLV))

static inline uint16_t        get_tmgbstlv_offset(char *pkt, bool vlan)  { return get_l4data_offset(pkt,vlan); }
static inline uint16_t        get_tmsynctlv_offset(char *pkt, bool vlan) { return get_l4data_offset(pkt,vlan); }
static inline uint16_t        get_tmsitlv_offset(char *pkt, bool vlan)   { return get_l4data_offset(pkt,vlan); }

static inline TMGbsTLV*       get_tmgbstlv_ptr(char *pkt, bool vlan)  { return (TMGbsTLV*)      get_l4data_ptr(pkt,vlan); }
static inline TMSyncTLV*      get_tmsynctlv_ptr(char *pkt, bool vlan) { return (TMSyncTLV*)     get_l4data_ptr(pkt,vlan); }
static inline TMSchedInfoTLV* get_tmsitlv_ptr(char *pkt, bool vlan)   { return (TMSchedInfoTLV*)get_l4data_ptr(pkt,vlan); }

#ifdef __cplusplus
}
#endif

#endif /* _ORION_TMINT_H_ */
