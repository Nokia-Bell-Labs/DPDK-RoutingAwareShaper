/* tmDefs.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef _TM_DEFS_H_
#define _TM_DEFS_H_

#define ORION_APP_TM

// Includes (from ../common/OrionTMInt.h)
#include <stdbool.h>
#include "../common/OrionDpdk.h"
#include "../common/OrionPktDefs.h"
#include "../common/OrionP4Int.h"

#define RTE_LOGTYPE_PARSER RTE_LOGTYPE_USER1

// Conditional code inclusions
#define INCLUDE_MEMORY_BARRIERS
#define TEST_RX_BURST_PERFORMANCE

// Time conversions
#define NSEC_PER_SEC                    1E9         	// Nano seconds per second
#define USEC_PER_SEC                    1E6         	// Micro seconds per second
#define USEC_PER_MSEC                   1E3         	// Micro seconds per milli-second
#define RTE_RDTSC(_epoch)               (rte_rdtsc() - _epoch)

// Packet and frame definitions
#define PKTLEN_GBS                      400
//#define ETHER_FRAME_OVERHEAD            24          	// Empirical value from test result for 25G on ensaw1
//#define ETHER_FRAME_OVERHEAD            (8+20+30)
#define UDP_HDR_LEN			8
#define IP_HDR_LEN			20
#define ETH_HDR_LEN			14
#define VLAN_HDR_LEN			4
#define ETH_CRC_LEN			4
#define PREAMBLE_LEN			7
#define SFD_LEN				1
#define IPG_LEN				12
#define ETHER_DL_FRAME_OVERHEAD		(UDP_HDR_LEN + IP_HDR_LEN + ETH_HDR_LEN + VLAN_HDR_LEN)   // +46 
#define ETHER_PHY_FRAME_OVERHEAD	(ETH_CRC_LEN + PREAMBLE_LEN + SFD_LEN + IPG_LEN)          // +24


// Conditional definition of the number of hops depending on the specific use of TM9
//#define  POC_XC_AND_FABRIC		// Select this option if TM9 is used with a cross-connect and  leaf-spine fabric
#define  POC_XC				// Select this option if TM9 is used with a cross-connect only

#if defined   POC_XC_AND_FABRIC
#define  NUM_HOPS                       5
#define  TELEMETRY_DATA_LEN		80	// (NUM_HOPS * 16): hop x 16 bytes (main header already present)
#elif defined POC_XC
#define  NUM_HOPS			1	// Only one more telemetry header to be added
#define  TELEMETRY_DATA_LEN		16	// (NUM_HOPS * 16): hop x 16 bytes (main header already present)
#endif

// WARNING! The above must be revised for every new setup!

// System RX/TX definitions
#define NUM_RXQUEUES_MAX                8
#define TM_RX_PKT_BURST_MAX             16

#define NUM_PORTSPERSCHED_MAX           2           	//
//#define NUM_PORTSPERSCHED_MAX           1           	//
#define TXDESC_PER_QUEUE_MAX            48          	// Software TxDesc max of above hw limites to prvent underruns

// Statistics definitions
#define STATS_TIMER_PERIOD_DEFAULT      10          	// 10 seconds
#define STATS_TIMER_PERIOD_MAX          86400       	// 1 day max


// Configuration file definitions
#define SCHED_CONFIG_FILE_LEN_MAX  	128             // Scheduler Config File max len (from --pfc argument), including NULL
#define INTF_CONFIG_FILE_LEN_MAX   	128             // Interface Config File max len (from --pfc argument), including NULL
#define STREAM_CONFIG_FILE_LEN_MAX   	128             // Stream Config File max len (from --pfc argument), including NULL

// Scheduler definitions (from ../common/OrionTMInt.h)
#define NUM_SCHED_MAX              	2
#define NUM_TIMESLOTS_MAX          	40000           // Number of time slots in periodic service sequence
#define TM_NUM_RX_RINGS            	16            // Number of shared rx rings (aka scheduler queues) by GBS traffic
//#define TM_NUM_RX_RINGS            	2048            // Number of shared rx rings (aka scheduler queues) by GBS traffic
#define TM_NUM_TX_RINGS            	1               // Hardcoded to 1 by implementation
#define NUM_GBSQUEUES_MAX          	TM_NUM_RX_RINGS
//#define NUM_GBSQUEUES_MAX          	4096
#define TM_NUM_CLASSES             	8               // Number of traffic classes to analyze
#define TM_CLASS_MASK              	0x7             // Mask on vlan ID to get traffic class
#define QUEUES_PER_BUNDLE_MAX      	16              // Max number of queues in a queue bundle

// Stream definitions
#define NUM_STREAMS_MAX			TM_NUM_RX_RINGS
//#define NUM_STREAMS_MAX               4096
#define STREAM_ID_TO_IDX(streamId)      (streamId - sc->streamsBaseNum + 1)
#define PKT_SIZE_DEFAULT                512             // Packet size used to compute timing of empy queue

// Classifier type definitions
#define CLASSIFIER_TYPE_MIN		(uint16_t)1
#define VLANID_SRCMAC_CLASSIFIER	(uint16_t)1
#define VLANID_TTL_CLASSIFIER		(uint16_t)2
#define SRCPORT_CLASSIFIER		(uint16_t)3
#define CLASSIFIER_TYPE_MAX		(uint16_t)3


/* --- ECN support ---------------------------------------------- */
extern uint32_t ecn_mark_threshold; 

// ************************************************
// State Enumerations (from ../common/OrionTMInt.h)
// ************************************************

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

enum StreamType_e
{
  STREAM_TYPE_UNKNOWN,
  STREAM_TYPE_BW_DOMINATE,
  STREAM_TYPE_LAT_DOMINIATE,
  STREAM_TYPE_OTHER
};

// ******************************************************
// Configuration Parameters (from ../common/OrionTMInt.h)
// ******************************************************

#define RX_FLOWS_MAX  8
typedef struct RxFlow_s {
  uint16_t flowId;
  uint16_t queueId;                    // mapping of vlanId to dpdk rx queue Id
  uint16_t vlanTci;                    // vlanId (lower 12 bits) + priority
  uint16_t vlanTciMask;                // mask (0xffff) for full match
} RxFlow;

typedef struct StreamCfg_s
{
  int             streamId;
  uint8_t         srcIP[4];       
  uint8_t         dstIP[4];
  float           rate;
  float           latency;
  uint16_t        pktsize;
  uint32_t        vlanId;
  uint32_t        vlanPri;
  uint8_t         ttl;
  uint8_t         protocol;
  float           avg_on_time;    // only relevant for type 2 flows
  float           avg_off_time;   // only relevant for type 2 flows
  uint8_t         dominance;      // enum StreamType_e
} StreamCfg;

typedef struct PathConf_s 
{
  int32_t  numTimeslots;               // Number of timeslots for this path, derived from cfg file
  uint32_t schedRate;                  // Scheduling rate of the path
} PathConf;

typedef struct BundleConf_s 
{
  uint16_t bid;                        // Bundle id (for convenience)
  uint16_t numQueues;                  // Number of queues in this bundle; max QUEUES_PER_BUNDLE
  int32_t  numTimeslots;               // Number of timeslots for this bundle, derived from csv cfgfile
  uint32_t schedRate;                  // Scheduling rate of queue
  uint16_t queues[QUEUES_PER_BUNDLE_MAX];  // Map of flow queues to bundle
  uint16_t pathId;		       // Path of the bundle
} BundleConf;

typedef struct RunConf_s 
{
  uint8_t  initMask;                   // enum SchedInit_e
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
  uint8_t  ipVer;
  uint8_t  dscp : 6;
  uint8_t  ecn  : 2;
  uint16_t dstPort;                    // destination port address, i.e. of TA app
  bool     hwChksumOffload;
  bool     updateSeqNo;
} IntfConf;

typedef struct SchedConf_s
{
  uint8_t  schedId;                    // index to this instance
  uint8_t  confId;		       // index to config settings for PSS, Bundle ... CRP
  time_t   lastUpdateTime;             // Time pss file was last updated
  bool     newConfig;                  // Set if new config file is detected
  uint8_t  schedMode;                  // SchedMode_e
  uint8_t  rxPort;
  uint8_t  txPort;
  uint8_t  rxCore;
  uint8_t  tmCore;
  uint8_t  txCore;
  uint32_t linkSpeedMbps;              // in mbps
  uint16_t timeslotsPerSeq;            // number of timeslots in a scheduling sequence  NS3:m_schedSlots
  uint16_t maxPktSize;                 // maximum size of a packet
  uint16_t rxBurstSize;                // rte_eth_rx_burst() limit
  uint32_t timeslotNsec;               // timeslot duration in nsec
  uint32_t timeslotTsc;                // timeslot duration in tsc ticks  NS3:m_slotDuration
  uint64_t tscHz;
  double   tscHzMeasured;
  uint64_t linkSpeedBpMTsc;             // Link speed in bits per million TSC tics
  uint16_t queuesNum;                   // number of logical queues; in case of bundling, this is the number of bundles
  uint16_t baseStreamId;                // number of first stream id; used for mapping to queues
  uint16_t classifierType;             // Type of classification used for queuing incoming packets [1, 3]
  uint32_t ecnThreshold;    

  
  uint16_t pss[2][NUM_TIMESLOTS_MAX];     // From csv file, Scheduling sequence of queues assignments indexed by fixed duration timeslot
  PathConf pathConf[2][NUM_GBSQUEUES_MAX]; // Path configuration from cfg file; number of bundles could equal NUM_QUEUES_MAX,
                                           // i.e. each flow in its own path
  BundleConf bundleConf[2][NUM_GBSQUEUES_MAX]; // Bundle configuration from csv file; number of bundles could equal NUM_QUEUES_MAX,
                                            // i.e. each flow in its own bundle
  /* config file  info */
  char     schedCfgFile[SCHED_CONFIG_FILE_LEN_MAX];
  char     intfCfgFile[INTF_CONFIG_FILE_LEN_MAX];
  char     streamCfgFile[STREAM_CONFIG_FILE_LEN_MAX];

  // stream config (from streams cfg file)
  int      numStreams;                 // derived from number of streams in file
  int      streamsBaseNum;             // stream number to stream position mapping; derived from streams file
  StreamCfg streamCfg[2][NUM_STREAMS_MAX+1]; // allow stream config updates

} __rte_cache_aligned SchedConf;


// VLAN lookup table entry, needed for packet classification based on VLAN ID and source MAC address
// In the Bosch setup, packets are classified based on the VLAN PCP first, and then on the VLAN ID and
// source MAC address.
typedef struct VlanLookupEntry_s
{
  uint16_t vlanId;			// VlanId, should match the table index
  struct rte_ether_addr	macaddr1;	// First source MAC address
  struct rte_ether_addr macaddr2;	// Second source MAC address
  uint16_t qid1;			// Queue ID of first MAC address
  uint16_t qid2;			// Queue ID of second MAC address
} VlanLookupEntry;

// ****************
// State Parameters
// ****************

// Credits
typedef struct CreditState_s
{
  int64_t  value;              	       // GBS credit in TSC tics  NS3:m_gbsCredit[]
  uint64_t lastRtsc;                   // NS3 m_gbsLtstTime NS3:m_gbsLtstTime[]
} CreditState;

typedef struct PathState_s
{
  CreditState pathCredit;
} PathState;

typedef struct BundleState_s
{
  CreditState bundleCredit;
} BundleState;

typedef struct QueueState_s
{
  uint8_t          qtype;              // QUEUE_TYPE_xxx
  uint16_t         qid;                // static queue #. For reference only
  CreditState      queueCredit;        // Queue credit parameters

  // For RX queue - note only 3 queues are set up - it may be better to create another data structure CRP
  uint32_t tsViolation;
  struct rte_ring *rxRing;
  struct rte_mbuf *nextRxRingEntry;// if not NULL, a dequeued rxRing entry that is pending
  uint16_t nextMbufId;

  struct rte_mbuf  *pktMbuf[TXDESC_PER_QUEUE_MAX];  // not used?
} QueueState;

typedef struct EnqueueThreadStats_s {
  uint64_t rxPkts;
  uint64_t rxqPkts[NUM_RXQUEUES_MAX];
  uint64_t rxBytes;                // not implemented
  uint64_t rxFrameBytes;           // not implemented
  uint64_t rxRingDrops;
  uint64_t tscEnqLcoreBusy;        // cumulative tsc ticks that enqueue lcore pkt processing was performed
  uint64_t tscEnqLcoreBusyDPDK;    // cumulative tsc ticks that enqueue lcore pkt processing by DPDK driver
  uint64_t tscEnqLcoreIdle;        // cumulative tsc ticks that enqueue lcore pkt processing was idle (i.e. busy wait)
} __rte_cache_aligned EnqueueThreadStats;

// Per-port statistics struct - These are runnint counnters that do nto get cleared.
typedef struct DequeueThreadStats_s {
  uint32_t timeslots;
  uint32_t timeslotsSkipped;           // number of timeslots skipped, ideally should be 0
  uint32_t timeslotsSkippedMax;        // max number of times slots skipped during period, again ideally should be 0
  uint32_t schedSequences;
  uint32_t slotRelinquished;
  uint32_t slotCreditSat;              // credit saturated, reset!
  uint32_t txRingDrops;
  uint32_t txPktSentRtn0;              // transmit api returned 0 (not sent)
  uint32_t txPktSentRtnX;              // transmit api returned >1 (more than specified)
  uint64_t txPkts;
  uint64_t txBytes;
  uint64_t txSchedBytes;               // representing bytes/time on physical layer, i.e. scheduling rate
  uint64_t txGBSPkts;
  uint64_t txEBSPkts;
  uint64_t txSyncPkts;
  uint64_t tscDeqLcoreBusy;            // cumulative tsc ticks that dequeue lcore pkt processing was performed
  uint64_t tscDeqLcoreIdle;            // cumulative tsc ticks that dequeue lcore pkt processing was idle (i.e. busy wait)
  uint64_t tscSchedErrMax;
  uint64_t tscSchedErrExc;
} __rte_cache_aligned DequeueThreadStats;

// Per-port statistics struct - These are runnint counnters that do nto get cleared.
typedef struct TxThreadStats_s {
  uint64_t txPktsDeq;
  uint64_t txPktsSent;
  uint64_t txBytes;
  uint64_t txSchedBytes;               // representing bytes/time on physical layer, i.e. scheduling rate
  uint64_t tscTxLcoreBusy;             // cumulative tsc ticks that dequeue lcore pkt processing was performed
  uint64_t tscTxLcoreIdle;             // cumulative tsc ticks that dequeue lcore pkt processing was idle (i.e. busy wait)
} __rte_cache_aligned TxThreadStats;

#define  STATS_DEQUEUE _deqstats       // DequeueThreadStats
#define  STATS_TX      _txstats        // TxThreadStats
#define STATS_ENQUEUE _enqstats        // EnqueueThreadStats

typedef struct SchedState_s
{
  uint8_t  schedId;                    // index to this instance
  uint16_t queuesNum;                  // copy of SchedConf::queuesNum
  uint16_t timeslotIdx;
  uint16_t timeslotIdxPrev;
  uint16_t timeslotIdxSeq;
  uint16_t txqId;
  uint16_t txqNum;
  uint32_t timeslotTsc;                // Copy of SchedConf::timeslotTsc
  uint64_t tscEpoch;
  uint64_t schedSeqDurationRtsc;       // During of a full scheduling sequence in Rtsc (Relative TSC ticks since Epoch)
  uint64_t tscSyncStart;
  uint64_t tscSyncEnd;
  struct timespec todSyncStart;
  struct timespec todSyncEnd;
  struct rte_ring *txRing;

  PathState   gbsPath[2][NUM_GBSQUEUES_MAX];
  BundleState gbsBundle[2][NUM_GBSQUEUES_MAX];
  QueueState  gbsQueue[2][NUM_GBSQUEUES_MAX];
  QueueState  ebsQueue[TM_NUM_CLASSES];	// Low-priority queues, indexed by the priority bits of the classification header
  
  uint32_t txPktsTotal;
  uint64_t timeslotsTotal;
  uint64_t schedSeqTotal;
  uint64_t schedSeqTotalPrev;

  //struct rte_mbuf *streamPktMbuf[NUM_PORTSPERSCHED_MAX][NUM_STREAMS_MAX];
  struct rte_mbuf *streamPktMbuf[2][NUM_STREAMS_MAX];  // Update stream cfg

  // use above alias for stats below
  char pad1 __rte_cache_aligned;
  EnqueueThreadStats  _enqstats;  char pad3 __rte_cache_aligned;
  DequeueThreadStats  _deqstats;  char pad2 __rte_cache_aligned;
  TxThreadStats       _txstats;   char pad4 __rte_cache_aligned;

} __rte_cache_aligned SchedState;

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

extern volatile bool forceQuit;
extern uint32_t   enabledPortsMask;

extern RunConf    runConf;
extern IntfConf   intfConf[];          // for multiple instances of interfaces.
extern SchedConf  schedConf[];         // for multiple instances of scheduler.
extern SchedState schedState[];        // for multiple instances of scheduler.
extern uint16_t	  schedClassifierType; // Classification method
extern VlanLookupEntry vlanid_table[]; // Lookup table for VLAN IDs

extern struct rte_mempool *pktmbufPool;

//extern int InitTraffic(SchedConf *sc, SchedState *ss);
extern void SchedThreadsDispatcher(void);
extern int StreamPktInit(uint8_t confId, uint8_t sid); // update for stream config 
extern int StreamRatesValidate(SchedConf *sc, uint8_t confId);
extern int parse_args(int argc, char **argv);
extern int ethdev_wait_all_ports_up(uint32_t portsMask, int maxSeconds);
extern int ethdev_init(uint32_t cpuSocket, uint32_t portsMask, RunConf *rc);
extern double tscClockCalibrate(bool verbose);

extern int app_parse_scf_cfgfile(SchedConf *sc, const char *cfgfile, uint8_t confId);
extern void mac_address_printf(struct rte_ether_addr *macaddr);

#endif  //  _TM_DEFS_H_
