/* tmSched.c 
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
*/

#include "tmDefs.h"
#include "tmBundle.h"
#include "tmStats.h"
#include "parserLib.h"
#include "../common/OrionLog.h"
#include <stdio.h> 
#include <rte_ip.h>

#include <stdint.h>
    
// dropping threshold
#define DROP_QUEUE_THRESHOLD  128

// #define ECN_MARK_THRESHOLD    4053

uint32_t ecn_mark_threshold = 0;




// debug
//#include "dumpLib.h"

int debugLog = 0;
int errorLog = 1;
int infoLog = 1;

uint16_t	schedClassifierType;
VlanLookupEntry vlanid_table[4096];

extern void SummaryEnqueueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops);
extern void SummaryDequeueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops);
extern void SummaryTxStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs);
extern void SummaryEtherPortStatsPrint(unsigned portId, uint32_t secs, uint64_t *drops);

extern IntfConf intfConf[];

static inline void
update_sched_mac(struct rte_mbuf *m, uint8_t sid)
{
    EtherHdr *eth = rte_pktmbuf_mtod(m, EtherHdr *);
    rte_ether_addr_copy(&intfConf[sid].schedPath.dstAddr, &eth->dst_addr);
    rte_ether_addr_copy(&intfConf[sid].schedPath.srcAddr, &eth->src_addr);
}


static inline void
maybe_mark_ecn(struct rte_mbuf *m, struct rte_ring *r)
{
    if (ecn_mark_threshold == 0 || unlikely(rte_ring_count(r) < ecn_mark_threshold))
        return;

    char     *pkt  = rte_pktmbuf_mtod(m, char *);
    bool      vlan = is_vlan_pkt(pkt);
    Ipv4Hdr  *ip   = get_ipv4hdr_ptr(pkt, vlan);

    /* Only IPv4 is shown – add IPv6 flow‑label code if you need it */
    uint8_t ecn = ip->type_of_service & 0x03;     /* lower 2 bits */
    if (ecn == 0x01 || ecn == 0x02) {             /* ECT(1) or ECT(0) */
        ip->type_of_service |= 0x03;              /* set CE (11b)     */
        ip->hdr_checksum     = 0;
        ip->hdr_checksum     = rte_ipv4_cksum(ip); 
    }
}




static void
dump_queues(const SchedState *ss, const SchedConf *sc, uint64_t now_us)
{
    /* Print timestamp first */
    printf("%" PRIu64, now_us);

    /* active GBS set == confId 0; skip Q0 (drop queue) */
    for (int q = 1; q < sc->queuesNum; q++)
        printf(",%u", rte_ring_count(ss->gbsQueue[0][q].rxRing));

    /* EBS classes 0‑(TM_NUM_CLASSES‑1) */
    for (int c = 0; c < TM_NUM_CLASSES; c++)
        printf(",%u", rte_ring_count(ss->ebsQueue[c].rxRing));

    printf("\n");
}


static uint64_t timerPeriodSec = STATS_TIMER_PERIOD_DEFAULT; /* default period is 10 seconds */
static uint64_t timerPeriodTsc = 0;

struct rte_mempool * sched_pktmbuf_pool = NULL;

// Overriding factor for path eligibility constraint
#define PATH_OVERRIDE_FACTOR    4

// Hardcoded for now!
#define HW_RXQUEUEID  0  // Was HW_TXQUEUE
#define NUM_TX_PORTS   1
#define PORT0    0


void mac_address_printf(struct rte_ether_addr *macaddr)
{
  uint8_t *locaddr = (uint8_t *)macaddr;
  printf("%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", locaddr[0], locaddr[1], locaddr[2], locaddr[3], locaddr[4], locaddr[5]);
  return;
}

static inline bool
mac_address_is_same(struct rte_ether_addr *mac1, struct rte_ether_addr *mac2)
{
  uint8_t *locaddr1 = (uint8_t *)mac1;
  uint8_t *locaddr2 = (uint8_t *)mac2;
  
  int ii = 0;
  for (; (ii < 6) && (locaddr1[ii] == locaddr2[ii]); ii++);
  return (ii == 6 ? true : false);
}

static unsigned
LcoreIdToSchedId(unsigned lcoreId)
{
  unsigned schedId=0;
  if (schedId > NUM_SCHED_MAX)
    rte_exit(EXIT_FAILURE, "ERROR: Scheduler instance %u on lcore %u exceeded max of %u\n", schedId, lcoreId, NUM_SCHED_MAX);
  return 0;
}

static void
SchedEnqueueThreadInit(void)
{
  // NOTE: if sharing SchedConf and SchedState between two threads is a performance issue!

  RunConf *rc = &runConf;

  // Wait till dequque thread is running
  while ((volatile bool)((rc->initMask & INIT_MASK_DEQRUNNING)==0) && !forceQuit)
    {
      // Need some work at gcc -O0 may optimize out loop and can;t exit on kill signal!!!
      rte_delay_us(30);  // increased to 20 from 10 (CRP)
    }
}

static void
SchedTxThreadInit(void)
{
  RunConf *rc = &runConf;

  // Wait until dequque thread is running
  while ((volatile bool)((rc->initMask & INIT_MASK_DEQRUNNING)==0) && !forceQuit)
    {
      // Need some work at gcc -O0 may optimize out loop and can;t exit on kill signal!!!
      rte_delay_us(10);
    }
}

// Create a single txRing for scheduler output to tx thread.
static void
CreateFifoRings(unsigned sid)
{
#define APP_RING_SIZE (262144)    // TESTING
#define MAX_NAME_LEN  32
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  char ring_name[MAX_NAME_LEN];

  uint32_t socket = rte_lcore_to_socket_id(sc->rxCore);  // Needed in case of NUMA. We should keep both lcores on same CPU socket!!
  uint32_t ringSize = APP_RING_SIZE;
  struct rte_ring *ring;

  printf("CreateFifoRings(): Creating Fifo Rings for GBS queues\n");
  for (unsigned i=0; i<TM_NUM_RX_RINGS; i++)
    {
      snprintf(ring_name, MAX_NAME_LEN, "rxRing-%u-q%u", sc->schedId, i);
      ring = rte_ring_lookup(ring_name);
      if (ring)
	rte_exit(EXIT_FAILURE, "ERROR: rxRing exist for sid%u queue#%u!\n", sid, i);
      ring = rte_ring_create(ring_name, ringSize, socket, RING_F_SP_ENQ | RING_F_SC_DEQ);
      if (ring == NULL)
	rte_exit(EXIT_FAILURE, "ERROR: rxRing create failed for sid%u queue#%u!\n", sid, i);
      // RX queue does not need to switch between configs so hard code confId
      ss->gbsQueue[0][i].rxRing = ring;
      //printf("CreateFifoRings(): Created %s size=%u, socket=%u, lcore=%u\n", ring_name, ringSize, socket, rte_lcore_id());
    }
  printf("CreateFifoRings(): LAST GBS Created %s size=%u, socket=%u, lcore=%u\n", ring_name, ringSize, socket, rte_lcore_id());

  printf("CreateFifoRings(): Creating Fifo Rings for EBS queues\n");
  for (unsigned i=0; i<TM_NUM_CLASSES; i++)
    {
      snprintf(ring_name, MAX_NAME_LEN, "rxRingEBS-%u-q%u", sc->schedId, i);
      ring = rte_ring_lookup(ring_name);
      if (ring)
	rte_exit(EXIT_FAILURE, "ERROR: rxRing exists for sid%u EBS queue#%u!\n", sid, i);
      ring = rte_ring_create(ring_name, ringSize, socket, RING_F_SP_ENQ | RING_F_SC_DEQ);
      if (ring == NULL)
	rte_exit(EXIT_FAILURE, "ERROR: rxRing create failed for sid%u EBS queue#%u!\n", sid, i);
      // RX queue does not need to switch between configs so hard code confId
      ss->ebsQueue[i].rxRing = ring;
      //printf("CreateFifoRings(): Created %s size=%u, socket=%u, lcore=%u\n", ring_name, ringSize, socket, rte_lcore_id());
    }
  printf("CreateFifoRings(): LAST EBS Created %s size=%u, socket=%u, lcore=%u\n", ring_name, ringSize, socket, rte_lcore_id());

  if (TM_NUM_TX_RINGS != 1)
    {
      rte_exit(EXIT_FAILURE, "ERROR: only single txRing supported!\n");  
    }
  else
    {
      unsigned i=0;
      snprintf(ring_name, MAX_NAME_LEN, "txRing-%u-q%u", sc->schedId, i);
      ring = rte_ring_lookup(ring_name);
      if (ring)
	rte_exit(EXIT_FAILURE, "ERROR: txRing exist for sid%u queue#%u!\n", sid, i);
      ring = rte_ring_create(ring_name, ringSize, socket, RING_F_SP_ENQ | RING_F_SC_DEQ);
      if (ring == NULL)
	rte_exit(EXIT_FAILURE, "ERROR: txRing create failed for sid%u queue#%u!\n", sid, i);

      ss->txRing = ring;
      printf("CreateFifoRings(): Created %s size=%u, socket=%u, lcore=%u\n", ring_name, ringSize, socket, rte_lcore_id());
    }
}

static void
SchedDequeueGbsInit(unsigned sid, uint8_t confId)
{
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  QueueState *qs;

  for (int q=0; q<sc->queuesNum; q++)
    {
      qs = &ss->gbsQueue[confId][q];
      //memset(qs, 0, sizeof(QueueState));  // Not required as done by SchedState init
      qs->qtype = QUEUE_TYPE_GBS;    
      qs->qid = (uint16_t) q & 0xffff;
    }
  CreateFifoRings(sid);
}

// Schedule configuration is initialized and used by dequeue thread
static unsigned
SchedDequeueInit(unsigned lcoreId)
{
  unsigned sid = LcoreIdToSchedId(lcoreId);

  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];

  ss->schedId  = sid;
  ss->tscEpoch = rte_rdtsc();
  ss->timeslotIdx   = 0;
  ss->timeslotIdxSeq = 0;
  ss->schedSeqTotal = 0;
  ss->queuesNum     = sc->queuesNum;
  ss->txqId         = runConf.txqId;    // all ports use the same tx qeueue id to schedule output pkts!
  ss->txqNum        = runConf.txqNum;    // info only

  SchedDequeueGbsInit(sid, 0);

  runConf.initMask |= INIT_MASK_STRUCT;

  return sid;
}


/* 
 * Scheduler classification: returns default qid QID_CATCHALL if not TM packet.
 * The qid for use by scheduler per rule defined by SHPS config file 
 * NOTE: tm3 had alternate qid assignment when testing with iperf3, see "PoCPhase1/tm3/README.TODO.TM3c" TEST9
 */
static inline uint16_t
SchedRxClassifyAndUpdatePkt(struct rte_mbuf *mbuf, uint64_t rxRtsc)
{
  uint16_t qid;
#define QID_CATCHALL  NUM_GBSQUEUES_MAX  // First queue in EBS set
#define QID_DROP      0
  
#if 0
  static uint32_t rxSeqNum[TM_NUM_RX_RINGS];
#endif
  
  char *pkt = rte_pktmbuf_mtod(mbuf, char *);
  bool vlan = is_vlan_pkt((char *)pkt);
  mbuf->hash.usr = 0;
  
  // Get the IP header, useful in any case
  Ipv4Hdr *ipv4Hdr = get_ipv4hdr_ptr(pkt, vlan);
  if (ipv4Hdr->version_ihl != 0x45 || (ipv4Hdr->next_proto_id!=IPPROTO_UDP && ipv4Hdr->next_proto_id!=IPPROTO_TCP))
    {
      // DEBUG
      // printf("Packet sent to DROP queue #1\n");
      // END DEBUG

      // AF250620: This condition for dropping packets may have to be refined. More specifically, it may have to be
      // based on a black list, rather than lack of compliance with a white list. The reason why it was introduced is
      // the reception of packets that seem to be stopping the reception of subsequent packets if transmitted back
      // to the switch. Keep an eye on anomalous behavior that this type of decision may cause in the future!
      return QID_DROP;
    }

  // Classify:
  // NOTE: The srcPort and dstPort are in same L4 offset location for UDP and TCP headers!! 
  typedef UdpHdr L4Hdr;
  L4Hdr *l4Hdr = get_udphdr_ptr(pkt, vlan);
  uint16_t dstPort = rte_cpu_to_be_16(l4Hdr->dst_port);
  uint16_t srcPort = rte_cpu_to_be_16(l4Hdr->src_port);

    

if (srcPort >= 32768) {
    return QID_CATCHALL;
}

if (srcPort >= 30000 && srcPort <= 31000) {
    return 1;  // send all 30000..31000 to GBS queue 1
}


#if 0
  if (srcPort==5201 || dstPort==5201)
    {
      // DEBUG
      //      printf("Packet sent to CATCHALL queue #2\n");
      // END DEBUG
      
      return QID_CATCHALL;  // iperf3 port
    }
#endif

  // For packet classification, only one of the following definitions muyst be present
  switch(schedClassifierType)
    {
    case VLANID_SRCMAC_CLASSIFIER:
      // VLAN ID first, then Source MAC Address
      if (vlan)
	{
	  // VLAN packet: proceed with the classification as planned

	  // Get the Source MAC address
	  EtherHdr *ethHdr = (EtherHdr *)pkt;
	  struct rte_ether_addr *macsrcaddr = &(ethHdr->src_addr);

	  // Get VLAN ID and PCP (VLAN TCI: PCP[3] - DEI[1] - VLAN ID[12])
	  VlanHdr *vlanHdr = get_vlanhdr_ptr(pkt);
	  uint16_t cputci = rte_cpu_to_be_16(vlanHdr->tci);
	  uint16_t vlanid  = GET_VLANID_FROM_TCI(cputci);
	  uint16_t vlanpcp = GET_VLANPRI_FROM_TCI(cputci);

	  // DEBUG
	  printf("VLAN TCI: %u  -  VLANID: %u  -  VLAN PCP: %u", cputci, vlanid, vlanpcp);
	  printf("  -  MAC Address: ");
	  mac_address_printf(macsrcaddr);
	  if (vlanid == (uint16_t)101)
	    {
	      printf(" Classifier MAC Address: ");
	      mac_address_printf(&(vlanid_table[vlanid].macaddr1));
	    }
	  printf("\n");
	  // END DEBUG

	  // DEBUG
	  // END DEBUG

	  // Classify based on VLAN ID and SRC MAC only if PCP = 7 (top-priority packet)
	  if (vlanpcp == 7)
	    {
	      if (mac_address_is_same(macsrcaddr, &(vlanid_table[vlanid].macaddr1)))
		{
		  qid = vlanid_table[vlanid].qid1;
		}
	      else if (mac_address_is_same(macsrcaddr, &(vlanid_table[vlanid].macaddr2)))
		{
		  qid = vlanid_table[vlanid].qid2;
		}
	      else
		{
		  // AF250619: The value of QID_CATCHALL points to the lowest-priority EBS queue
		  // It is chosen if the packet matches the VLAN ID but neither MAC address
		  qid = QID_CATCHALL;
		}
	    }
	  else
	    {
	      // Lower-priority packet: place in the corresponding class queue
	      qid = NUM_GBSQUEUES_MAX + vlanpcp;
	    }
	}
      else
	{
	  // Not a VLAN packet: send it to the catch-all queue (lowest-priority class queue)
	  qid = QID_CATCHALL;
	}
	  

      /*
	if(srcPort == 27)
	{
	printf("srcPort=%u vid=0x%x qid=%hhu vtci=0x%x\n",
	srcPort, rte_cpu_to_be_16(GET_VLANID_FROM_TCI(vlanHdr->tci)), qid,vtci);
	}
      */
      
      // DEBUG
      // printf("Packet sent to Queue %u #2\n", qid);
      // END DEBUG
	  
      break;

    case VLANID_TTL_CLASSIFIER:
      // VLAN ID first; if not, TTL in IP header
      /* NOTE:
       * 8/28/21: TG5 modified to support randomized srcPort for P4 WECMP feature and tna_orion_tm1 uses vlanId for
       *   Tofino TM qid/cos selection. Thus, do the same in TM5 to use vlanId and only use ipv4 ttl when pkt is not vlan.
       *   Both vlanId and TTL are configurable per stream with TG5 cfgfile.
       *   An alternative is partition 16 bit srcPort for qid and ecmpHash. This require modification of tna_orion_exp2.p4.
       */
      if (vlan)
	{
	  VlanHdr *vlanHdr = get_vlanhdr_ptr(pkt);
	  uint16_t cputci = rte_cpu_to_be_16(vlanHdr->tci);
	  uint16_t vlanid  = GET_VLANID_FROM_TCI(cputci);
	  uint16_t vlanpcp = GET_VLANPRI_FROM_TCI(cputci);

	  // DEBUG
	  printf("VLAN TCI: %u  -  VLANID: %u  -  VLAN PCP: %u", cputci, vlanid, vlanpcp);
	  printf("\n");
	  // END DEBUG

	  // Classify based on VLAN ID and SRC MAC only if PCP = 7 (top-priority packet)
	  if (vlanpcp == 7)
	    {
	      qid = rte_cpu_to_be_16(GET_VLANID_FROM_TCI(vlanHdr->tci)) % NUM_GBSQUEUES_MAX;
	      if (qid == 0)
		{
		  // QID 0 is never server: move the packet to the top-priority EBS queue
		  qid = NUM_GBSQUEUES_MAX + vlanpcp;
		}
	    }
	  else
	    {
	      // Send the packet to an EBS queue if not top-priority
	      qid = NUM_GBSQUEUES_MAX + vlanpcp;
	    }
	  // DEBUG
	  printf("Packet (VLAN) sent to Queue %u #3\n", qid);
	  // END DEBUG
	}
      else
	{
	  // Not a VLAN packet: classify based on TTL
	  qid = ipv4Hdr->time_to_live % NUM_GBSQUEUES_MAX;
	  
	  if (qid == 0)
	    {
	      // QID 0 is never served: move the packet to the top-priority EBS queue
	      qid = NUM_GBSQUEUES_MAX + TM_NUM_CLASSES - 1;
	    }
	  
	  // DEBUG
	  printf("Packet (not VLAN) sent to Queue %u #4\n", qid);
	  // END DEBUG
	}
      break;

    case SRCPORT_CLASSIFIER:
      // VLAN ID is ignored, look directly at Source Port (TCP or UDP)
      qid = srcPort % NUM_GBSQUEUES_MAX;

      if (qid == 0)
	{
	  // QID 0 is never served: move the packet to the top-priority EBS queue
	  qid = NUM_GBSQUEUES_MAX + TM_NUM_CLASSES - 1;
	}

      // CRP force PCP for test
      //if(srcPort >= 27) {
      //  VlanHdr *vlanHdr = get_vlanhdr_ptr(pkt);
      //      uint16_t vlanID = rte_cpu_to_be_16(GET_VLANID_FROM_TCI(vlanHdr->tci));
      //  vlanHdr->tci = rte_cpu_to_be_16(GET_VLANTCI(5, 0, vlanID));
      //}

      // DEBUG
      // printf("Packet sent to Queue %u #5\n", qid);
      // END DEBUG
      
      break;

    default:
      // Unknown classification criterion: send to catch-all queue
      printf("ERROR: Unknown Classification Method (%d)! - Packet sent to CATCHALL queue\n", schedClassifierType);
      qid = QID_CATCHALL;
      
      // DEBUG
      // printf("Packet sent to Queue %u #6\n", qid);
      // END DEBUG

      break;
    }
  
#if 0
  // Ingress Packet Metadata Update
  // Save classification info in mbuf for use by dequeue thread!
  // NOTE: We can use private data instead by reserving its space with rte_pktmbuf_pool_create()
  OrionMbufUsr omu;
  omu.u.addTMINT = ((ipv4Hdr->type_of_service >> 2)==DSCP_ORION_TM) ? 1 : 0;
  omu.u.vlan = vlan;
  omu.u.rsvd = 0;
  mbuf->hash.usr = omu.usr;
#endif
  // Comment out if not connected to XConnect switch that adds shim headers
#if 0
  bool addSeqno = ((ipv4Hdr->type_of_service >> 2)==DSCP_ORION_MDINT) ? 1 : 0;
  if (addSeqno)
    {
      // locate inserted P4 headers
      // verify P4 INT headers
      P4IntShimHeader *shimHdr = (P4IntShimHeader *) ((char *)l4Hdr + sizeof(UdpHdr));
      P4IntMDMdHeader *mdHdr   = (P4IntMDMdHeader *) ((char *)shimHdr + sizeof(P4IntShimHeader));

      // P4INT Headers Sanity Check
      uint16_t instrBP=rte_be_to_cpu_16(mdHdr->instrBitmap);
      if ((shimHdr->type != P4INT_TYPE_INTMD) || (shimHdr->npt != P4INT_NPT_PRESERVE_L4) ||
	  (mdHdr->version != P4INT_MD_VERSION) || (((mdHdr->hopML5bits&0x1f)*4) != sizeof(P4IntMDMdStack)) ||
	  (instrBP != P4INT_MD_INSTRBITMAP))
	printf("ERROR: ShimHdr: type=%u,npt=%u mdHdr:ver=%hhu,hopML=%hhu,remHopCnt=%hhu,instBP=%u\n", shimHdr->type, shimHdr->npt, mdHdr->version, mdHdr->hopML5bits&0x1f,mdHdr->remHopCnt,instrBP);
      else
	// increment seqno
	mdHdr->seqno = rxSeqNum[qid]++;
    }
#endif

  /* TLV Update if needed
   * TMGbsTLV can be inserted either by TG5 or TM5 when omu.u.addTMINT=true.
   * See slides on TM5 for its decision rules. However, the code below is authoritative as documentation may be not updated!! 
   */
#if 0
  if (omu.u.addTMINT)
    {
      TMGbsTLV *tlv = get_tmgbstlv_ptr(pkt, vlan);

      if (dstPort!=UDPPORT_ORION_TMINT)
	{
	  // Save original protocol type before SchedAddGbsTLVEncap() that moves headers which invalidates ipv4Hdr
	  uint8_t origProtocol = ipv4Hdr->next_proto_id;
	  ipv4Hdr->next_proto_id = IPPROTO_UDP;  // TLV is over UDP header that we are inserting
	  //Disabled safeguard to reduce overhead!   ipv4Hdr = NULL;  // safeguard

	  /* TG5 did not populate TLV header. Insert a new UDP header + TMGbsTLV.
	   * CAUTION: This operation has high cpu load as need to move original EtherHdr/VlanHdr/Ipv4Header to mbuf HEADROOM space!
	   */
	  //if (tlv->tlvType==TLV_TYPE_TMINT)
	  {
	    // Insert a new TMGbsTLV into our pkt!!
	    UdpHdr *udpHdr = SchedAddGbsTLVEncap(mbuf);
	    if (udpHdr == NULL)
	      {
		omu.u.addTMINT = false;
		printf("ERROR: Encap insert failed!\n");
		// FUTURE: increments TMINT insert failure count
		return qid;
	      }

	    // Initialize new UDP header for TLV. The original UDP header and payload are untouched as is after our inserted TLV
	    udpHdr->src_port = rte_cpu_to_be_16(qid);
	    udpHdr->dst_port = rte_cpu_to_be_16(UDPPORT_ORION_TMINT);
	    udpHdr->dgram_len = rte_cpu_to_be_16(sizeof(UdpHdr) + sizeof(TMGbsTLV));
	    udpHdr->dgram_cksum = 0;  // disabled
	    tlv = (TMGbsTLV *) ((char *)udpHdr + sizeof(UdpHdr));  // new TLV ptr
	  }

	  // Initialize TLV header
	  tlv->tlvType = TLV_TYPE_TMINT;
	  tlv->nextProtocol = origProtocol;  // Preserved original L4 protocol
	  tlv->tlvLength = sizeof(TMGbsTLV);  // host endian format
	  tlv->tmsHdr.version = 2;
	  tlv->tmsHdr.nodeId = 1;
	  tlv->tmsHdr.pktAction = PKTACTION_SCHED_GBSUPDT;
	}
      tlv->queueId = qid;
      tlv->tsRtscTx = rxRtsc;  // cache it with rxRtsc for next TLV update stage
      tlv->tmsHdr.seqno = rxSeqNum[qid]++;

#if 0   // DEBUG
      {
	char *pkt = rte_pktmbuf_mtod(mbuf, char *);
	bool vlan = is_vlan_pkt((char *)pkt);
	Ipv4Hdr *ipv4Hdr = get_ipv4hdr_ptr(pkt, vlan);
	uint16_t totalLen = rte_cpu_to_be_16(ipv4Hdr->total_length);
	printf("DEBUG: seqNo=%u, qid=%hhu, ttl=%hhu, len=%u\n", tlv->tmsHdr.seqno,qid,ipv4Hdr->time_to_live,totalLen);
      }
#endif

    }
#endif

  // DEBUG
  //printf("Final queue selection: x%ux\n", qid);
  // END DEBUG
  
  return qid;
}

// qid is scheduler's queue specified by SchedRxClassifyPkt() and "--pfc" config file
static inline void
SchedRxEnqueuePkt(SchedState *ss, uint16_t qid, struct rte_mbuf *mbuf)
{
  QueueState *qs;

  if (qid < NUM_GBSQUEUES_MAX)
    {
      if (likely(qid == 0))
	{
	  // Drop the packet if destined for Queue 0
	  printf("Dropped packet to QID 0\n");
	  rte_pktmbuf_free(mbuf);
	  ss->STATS_ENQUEUE.rxRingDrops++;
	  return;
	}
      qs = &ss->gbsQueue[0][qid];

      // DEBUG
      // printf("Enqueuing to GBS queue %u\n", qid);
      // END DEBUG
	
    }
  else
    {
      qs = &ss->ebsQueue[qid - NUM_GBSQUEUES_MAX];

      // DEBUG
      // printf("Enqueuing to EBS queue %u\n", qid - NUM_GBSQUEUES_MAX);
      // END DEBUG
    }

        

  ss->STATS_ENQUEUE.rxBytes += mbuf->pkt_len;
  ss->STATS_ENQUEUE.rxFrameBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD);

  
  // Reference code from DPDK_TM/qosms_demo10/
  if (unlikely(rte_ring_sp_enqueue(qs->rxRing, (void *)mbuf) != 0))
    {
      rte_pktmbuf_free(mbuf);
      ss->STATS_ENQUEUE.rxRingDrops++;
      return;  // caller does not check for return condition!
    }
  maybe_mark_ecn(mbuf, qs->rxRing);

  // DEBUG
  //printf("DBG: SchedRxEnqueuePkt(qid %u) enqueued mbuf %p, entries = %u\n", qid, mbuf, rte_ring_count(qs->rxRing));
  //printf("DBG: SchedRxEnqueuePkt: qid %u  pktsize: %u  qlen %u\n", qid, mbuf->pkt_len, rte_ring_count(qs->rxRing));
  // END DEBUG
}


/* 
 * Enqueue thread:
 * Enqueue Rx pkts to one of the rxRing (i.e. queues) for GBS/EBS scheduler or SRR scheduler.
 * If L2FWD: then bypass the other threads and forward packet directly to DPDK tx driver
 */
static void
SchedEnqueueThread(unsigned lcoreId)
{
  unsigned sid = LcoreIdToSchedId(lcoreId);
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];

  uint16_t rxPort = sc->rxPort;
  uint16_t rxBurstSize = sc->rxBurstSize;
  struct rte_mbuf *rxMbufs[rxBurstSize] __rte_cache_aligned;
  uint16_t rxqNum = runConf.rxqNum; 

  SchedEnqueueThreadInit();

  uint64_t epoch = ss->tscEpoch;
  runConf.initMask |= INIT_MASK_ENQRUNNING;

  RTE_LOG(INFO, SCHED, "Enqueue thread completed init (0x%02x) on lcore %u\n", runConf.initMask, lcoreId);

#if 0
  if (sc->schedMode == SCHED_MODE_L2FWD)
    {
      SchedEnqueueThreadSimpleL2fwd(lcoreId);
      printf("SchedEnqueueThread() exiting!\n");
      return;
    }
#endif

  while (!forceQuit)
    {
      uint64_t rtscCurr = RTE_RDTSC(epoch);
      int nb_rx = 0;
      uint16_t q;

      // NOTE: For now, assume higher q# has higher priority
      q = rxqNum - 1;
      while (1)
	{
	  nb_rx = rte_eth_rx_burst(rxPort, q, rxMbufs, rxBurstSize);
	  if (nb_rx > 0 || q == 0)
	    {
	      break;
	    }
	  q--;
	}

      // DEBUG
      //printf("Packets extracted from RX queue: %d\n", nb_rx);
      // END DEBUG
      
      uint64_t rxRtsc  = RTE_RDTSC(epoch);
      if (likely(nb_rx > 0))
	{
	  /* 6/21/21: Testing show no performance improvement at 18Gbps with prefetch below
	   * rte_prefetch0(rte_pktmbuf_mtod(rxMbufs[0], void *));    // prefetch the first mbuf to optimize most frequent case!
	   */
	  ss->STATS_ENQUEUE.rxqPkts[q] += nb_rx;
	  ss->STATS_ENQUEUE.rxPkts += nb_rx;

	  for(int i = 0; i < nb_rx; i++)
	    {
	      // WARNING: Pkt headers may be modified on return when insert new headers for TMGbsTLV.
	      // Do not use any old pkt pointers!
	      uint16_t qid = SchedRxClassifyAndUpdatePkt(rxMbufs[i], rxRtsc);  // scheduler queue for SHPS forwarding

	      // DEBUG
	      //printf("About to call SchedRxEnqueuePkt\n");
	      // END DEBUG
	      
	      SchedRxEnqueuePkt(ss, qid, rxMbufs[i]);  // mbuf may be freed upon return when ring is full!
	    }
	}

      uint64_t tscDelta = RTE_RDTSC(epoch) - rtscCurr;
      if (likely(nb_rx != 0))
	{
	  ss->STATS_ENQUEUE.tscEnqLcoreBusy += tscDelta;
	}
      else
	{
	  ss->STATS_ENQUEUE.tscEnqLcoreIdle += tscDelta;
	}

    }
  printf("SchedEnqueueThread() exiting!\n");
}

enum DeqStateMask_e
  {
    DEQ_STATE_MASK_NEW_TIMESLOT = 0x01,
    DEQ_STATE_MASK_NEW_SCHEDSEQ = 0x02,
    DEQ_STATE_MASK_RELINQUISHED = 0x04,
    DEQ_STATE_MASK_BORROWEDCRED = 0x08,
    DEQ_STATE_MASK_NOTSENT      = 0x10,
    DEQ_STATE_MASK_EBS          = 0x20
  };

// Idle for duration of last scheduled pkt to keep Ethernet Controller hw fifo at low level!!!
static inline void
SchedDequeueThreadIdleWait(SchedState *ss, uint64_t txtimeTsc, uint64_t rtscCurr)
{
  uint64_t waitCount = 0;
  uint64_t epoch = ss->tscEpoch;
  if (likely(txtimeTsc != 0))
    {
      SchedConf  *sc = &schedConf[0];  // temporary: only one scheduler assumed
      uint64_t rtscNow  = RTE_RDTSC(epoch);
      uint64_t tscBusy = rtscNow - rtscCurr;
      ss->STATS_DEQUEUE.tscDeqLcoreBusy += tscBusy;

      uint64_t nextPktTimeRtsc = rtscCurr + txtimeTsc;

      uint64_t tscIdle = 0;
      uint64_t maxTsc = rtscCurr + sc->timeslotTsc;
      if (nextPktTimeRtsc > rtscNow)
	{
	  tscIdle = nextPktTimeRtsc - rtscNow;
	  ss->STATS_DEQUEUE.tscDeqLcoreIdle += tscIdle;
	}

      uint64_t schedErr = 0;
      if (rtscNow > maxTsc)
        schedErr = rtscNow - rtscCurr;
      if (waitCount > ss->STATS_DEQUEUE.tscSchedErrMax)
        ss->STATS_DEQUEUE.tscSchedErrMax = waitCount;
      if (schedErr > 0)
        ss->STATS_DEQUEUE.tscSchedErrExc++;
    }
  else
    {
      // No pkts were available. Update idle time then try again!
      uint64_t tscIdle = RTE_RDTSC(epoch) - rtscCurr;
      ss->STATS_DEQUEUE.tscDeqLcoreIdle += tscIdle;
    }
}

#if 0
// Simple Round Robin Dequeue - used to test dequeue thruput with minimum overhead
static void
SchedDequeueThreadSimpleRR(unsigned lcoreId)
{
  unsigned sid = LcoreIdToSchedId(lcoreId);
  struct rte_mbuf *mbuf;
  // RUNMODE_NORMAL below
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  uint64_t epoch = ss->tscEpoch;
  uint16_t qid, qidPrev=0;

  printf("====== Dequeue Thread running Simple Round Robin Scheduler ======\n");

  struct rte_ring *rxRingCached[TM_NUM_RX_RINGS];
  for (int i=0; i<TM_NUM_RX_RINGS; i++)
    rxRingCached[i] = ss->gbsQueue[i].rxRing;

  bool hasRunLimit = (runConf.maxRunPkts!=0 || runConf.maxRunTimeslots!=0);  // Run time is constrained by # of packets or # of timeslots
  if (hasRunLimit)
    INFOLOG("Run duration Limited with maxPkts=%u or maxTimeslots=%u (0 for unlimited)\n", runConf.maxRunPkts, runConf.maxRunTimeslots);

  while (!forceQuit)
    {
      uint64_t rtscCurr = RTE_RDTSC(epoch);  // NS3:schedtime

      // Round Robin search for next available pkt in all ring buffers
      qid=qidPrev;
      while (1)
	{
	  int n = rte_ring_sc_dequeue(rxRingCached[qid], (void **) &mbuf);
	  if (n == 0)
	    {
	      // Found a pkt
	      break;
	    }
	  else
	    {
	      //qid = ++qid % sc->queuesNum; 
	      ++qid; qid = qid % sc->queuesNum;   // split to avoid compiler warning
	      if (qid == qidPrev)
		{
		  // gone thru all ring fifo queues and no pkts!
		  mbuf = NULL;  // to be sure!
		  break;
		}
	    }
	}
      qidPrev = qid;

      uint64_t txtimeTsc=0;
      if (mbuf)
	{
	  int rval = rte_ring_sp_enqueue(ss->txRing, (void *)mbuf);

	  // Update tx statistics for forwarded pkts
	  if (likely(rval==0))
	    {
	      txtimeTsc = ((mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN) * 8 * 1E6) / sc->linkSpeedBpMTsc;

	      // DEBUG: The following does not seem to happen ever (not included in code)
	      //printf("TX stats: txtimeTsc: %u  pkt_len: %u, ETHER_PHY_FRAME_OVERHEAD: %u  linkSpeed: %u  \n",
	      //	     txtimeTsc, mbuf->pkt_len, ETHER_PHY_FRAME_OVERHEAD, sc->linkSpeedBpMTsc);
	      // END DEBUG
	      
	      ss->STATS_DEQUEUE.txPkts++;
	      ss->STATS_DEQUEUE.txBytes += mbuf->pkt_len;
	      ss->STATS_DEQUEUE.txFrameBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD);
	    }
	  else
	    {
	      ss->STATS_DEQUEUE.txRingDrops++;
	      rte_pktmbuf_free(mbuf); // silent drops w/o tx retries
	    }
	}

      // FUTURE: Move this block to SchedDequeueThreadIdleWait()?
      if (unlikely(hasRunLimit &&
		   ((runConf.maxRunPkts!=0 && ss->txPktsTotal >= runConf.maxRunPkts) ||
		    (runConf.maxRunTimeslots!=0 && ss->timeslotsTotal >= runConf.maxRunTimeslots))) )
	{
	  forceQuit = true;
	  if (runConf.maxRunPkts!=0 && ss->txPktsTotal >= runConf.maxRunPkts)
	    INFOLOG("Run terminated due to maxRunPkts limit of %u\n", runConf.maxRunPkts);
	  if (runConf.maxRunTimeslots!=0 && ss->timeslotsTotal >= runConf.maxRunTimeslots)
	    INFOLOG("Run terminated due to maxRunTimeslots limit of %u\n", runConf.maxRunTimeslots);
	}

      SchedDequeueThreadIdleWait(ss, txtimeTsc, rtscCurr);
    }
}
#endif

static void
SchedDequeueThreadDCB_Q(unsigned lcoreId)
{
  unsigned sid = LcoreIdToSchedId(lcoreId);
  // RUNMODE_NORMAL below
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  uint64_t epoch = ss->tscEpoch;
  uint32_t timeslotTsc = sc->timeslotTsc;
  uint16_t timeslotsPerSeq = sc->timeslotsPerSeq;
  int64_t overrideths = (int64_t) (timeslotTsc * timeslotsPerSeq / PATH_OVERRIDE_FACTOR);
  
  bool hasRunLimit = (runConf.maxRunPkts!=0 || runConf.maxRunTimeslots!=0);  // Run time is constrained by # of packets or # of timeslots
  if (hasRunLimit)
    INFOLOG("Run duration Limited with maxPkts=%u or maxTimeslots=%u (0 for unlimited)\n", runConf.maxRunPkts, runConf.maxRunTimeslots);
  
  // DCB_Q dequeue loop
  while (!forceQuit)
    {
    
#ifdef INCLUDE_MEMORY_BARRIERS
      rte_mb();
#endif


      uint8_t  deqStates=0;                                            // DEQ_STATE_MASK_xxx
      uint64_t rtscCurr = RTE_RDTSC(epoch);                            // NS3:schedtime

      uint64_t rtscRxDeq=0;
      uint8_t  pktType = PKTTYPE_UNKNOWN;
      
      struct rte_mbuf *mbuf;

      ss->timeslotsTotal = rtscCurr / timeslotTsc;                     // total number of timeslots processed (for metrics?)
      ss->schedSeqTotal  = ss->timeslotsTotal / timeslotsPerSeq;       // total number of sequences processed (for metrics?)

      ss->timeslotIdx = ss->timeslotsTotal % timeslotsPerSeq;          // Index of timeslot corresponding to current time

      if (ss->timeslotIdx != ss->timeslotIdxPrev)
	{
	  //ss->timeslotIdxSeq = (ss->timeslotIdxSeq + 1) % timeslotsPerSeq;
	  deqStates |= DEQ_STATE_MASK_NEW_TIMESLOT;
	  ss->STATS_DEQUEUE.timeslots++;
	  //if(ss->timeslotIdxSeq != ((ss->timeslotIdxPrev + 1) % timeslotsPerSeq))
	  //printf("Timeslot skipped current %d vs previous %d\n",ss->timeslotIdxSeq,ss->timeslotIdxPrev);
	  uint16_t timeslotDiff;
	  if (ss->timeslotIdx < ss->timeslotIdxPrev)
	    {
	      timeslotDiff = timeslotsPerSeq + ss->timeslotIdx - ss->timeslotIdxPrev;
	    }
	  else
	    {
	      timeslotDiff = ss->timeslotIdx - ss->timeslotIdxPrev;
	    }

	  // Checking if any timeslot was skipped since the last visit
	  if (timeslotDiff > 1)
	    {
	      ss->STATS_DEQUEUE.timeslotsSkipped++;
	    }
	  if (timeslotDiff > ss->STATS_DEQUEUE.timeslotsSkippedMax)
	    {
	      ss->STATS_DEQUEUE.timeslotsSkippedMax = timeslotDiff;
	    }

	  //ss->timeslotIdxPrev = ss->timeslotIdxSeq;
	  ss->timeslotIdxPrev = ss->timeslotIdx;

	  // Check if new scheduling sequence
	  if (ss->schedSeqTotal != ss->schedSeqTotalPrev)
	    {
	      deqStates |= DEQ_STATE_MASK_NEW_SCHEDSEQ;
	      ss->STATS_DEQUEUE.schedSequences++;
	      ss->schedSeqTotalPrev = ss->schedSeqTotal;
	    }
	}

      uint16_t gbsBundleId = sc->pss[sc->confId][ss->timeslotIdx]; // id of scheduled bundle; NS3:schedqueueid (in DCB_Q)

      // Bundle configuration and state
      BundleConf *bc = &(sc->bundleConf[sc->confId][gbsBundleId]);
      BundleState *bs = &(ss->gbsBundle[sc->confId][gbsBundleId]);
      
      // Path configuration and state
      uint16_t gbsPathId = bc->pathId;
      PathConf *pc = &(sc->pathConf[sc->confId][gbsPathId]);
      PathState *ps = &(ss->gbsPath[sc->confId][gbsPathId]);
      
      // AF DEBUG
      /*
      printf("t: %lu timeslot: %u Candidate Bundle: %u\n", rtscCurr,  ss->timeslotIdx, gbsBundleId);
      */
      // END DEBUG
      
      // if a bundle is scheduled, ...
      if (gbsBundleId > 0)
	{
	  // Update the state of the path if existing (i.e., hierarchical shaper)
	  if (gbsPathId > 0)		// Path #0 is not a real path
	    {
	      increasePathCredit(sc, ps, pc->numTimeslots, rtscCurr);
	    }

	  // Update the bundle credit for this bundle
	  increaseBundleCredit(sc, bs, bc->numTimeslots, rtscCurr);

	  // The current slot is not empty: see if the target bundle can be scheduled
	  // AF240927: Added here the new condition on path eligibility
	  bool queuesAreEmpty = bundleQueuesAreEmpty(ss, bc);
          if ((bs->bundleCredit.value < 0) ||
              ((gbsPathId > 0) && (bs->bundleCredit.value < overrideths) && (ps->pathCredit.value < 0)) ||
	      (queuesAreEmpty == true))
	    {
	      // AF241221: I added here the condition on the occupancy state of the bundle, for consistency with the simulation code
	      // and because it makes sense in general.
	      
	      // No credits for the bundle or for its path: the bundle relinquishes
	      // the remaining portion of the slot

	      // DEBUG
	      /*
	      printf("t: %lu timeslot: %u - candidate bundle %u not selected: bc %ld - path ID: %u - pc: %ld  queuesEmpty: %d\n",
		     rtscCurr,  ss->timeslotIdx,
		     gbsBundleId, bs->bundleCredit.value,
		     gbsPathId, ps->pathCredit.value, queuesAreEmpty);
	      */
	      // END DEBUG
	      /*
	      if ((bs->bundleCredit.value >= 0) && (ps->pathCredit.value < 0))
		{
		  printf("DEBUG: eligible bundle %u (%ld) not selected because of path %u ineligibility (%ld)!\n",
			 gbsBundleId, bs->bundleCredit.value,
			 gbsPathId, ps->pathCredit.value);
		}
	      */
	      // END DEBUG
	      
	      // Advance the selection to the next slot
	      ss->timeslotIdxPrev = ss->timeslotIdx;
	      ss->timeslotIdx++;
	      deqStates |= DEQ_STATE_MASK_NEW_TIMESLOT;
	      ss->STATS_DEQUEUE.timeslots++;
	      if (ss->timeslotIdx >= timeslotsPerSeq)
		{
		  ss->timeslotIdx = ss->timeslotIdx - timeslotsPerSeq;
		  deqStates |= DEQ_STATE_MASK_NEW_SCHEDSEQ;
		  ss->STATS_DEQUEUE.schedSequences++;
		  ss->schedSeqTotalPrev = ss->schedSeqTotal;
		}

	      // Update the GBS credits for the bundle of the newly identified slot.
	      gbsBundleId = sc->pss[sc->confId][ss->timeslotIdx];                 // id of next scheduled bundle
	      bc = &(sc->bundleConf[sc->confId][gbsBundleId]);
	      bs = &(ss->gbsBundle[sc->confId][gbsBundleId]);

	      gbsPathId = bc->pathId;
	      pc = &(sc->pathConf[sc->confId][gbsPathId]);
	      ps = &(ss->gbsPath[sc->confId][gbsPathId]);

	      // If a new bundle is found...
	      if (gbsBundleId > 0)
		{
		  // ... update its bundle credit
		  increaseBundleCredit(sc, bs, bc->numTimeslots, rtscCurr);

		  // If the bundle has a path...
		  if (gbsPathId > 0)
		    {
		      // ... update its path credit
		      increasePathCredit(sc, ps, pc->numTimeslots, rtscCurr);
		    }
		}
	    }
	}
      else
	{
	  // AF250623: If the current slot is assigned to the virtual empty queue, the GBS queue
	  // of the next slot should be checked for service eligibility before the service is granted to
	  // a lower-priority queue. Otherwise, the queues that follow virtual empty queue services may
	  // suffer for excessive services given to the lower-priority queue, becasue there is no credit
	  // maintenance for the virtual empty queue.
	  
	  // Advance the selection to the next slot
	  ss->timeslotIdxPrev = ss->timeslotIdx;
	  ss->timeslotIdx++;
	  deqStates |= DEQ_STATE_MASK_NEW_TIMESLOT;
	  ss->STATS_DEQUEUE.timeslots++;
	  if (ss->timeslotIdx >= timeslotsPerSeq)
	    {
	      ss->timeslotIdx = ss->timeslotIdx - timeslotsPerSeq;
	      deqStates |= DEQ_STATE_MASK_NEW_SCHEDSEQ;
	      ss->STATS_DEQUEUE.schedSequences++;
	      ss->schedSeqTotalPrev = ss->schedSeqTotal;
	    }
	  
	  // Update the GBS credits for the bundle of the newly identified slot.
	  gbsBundleId = sc->pss[sc->confId][ss->timeslotIdx];                 // id of next scheduled bundle
	  bc = &(sc->bundleConf[sc->confId][gbsBundleId]);
	  bs = &(ss->gbsBundle[sc->confId][gbsBundleId]);
	  
	  gbsPathId = bc->pathId;
	  pc = &(sc->pathConf[sc->confId][gbsPathId]);
	  ps = &(ss->gbsPath[sc->confId][gbsPathId]);
	  
	  // If a new bundle is found...
	  if (gbsBundleId > 0)
	    {
	      // ... update its bundle credit
	      increaseBundleCredit(sc, bs, bc->numTimeslots, rtscCurr);
	      
	      // If the bundle has a path...
	      if (gbsPathId > 0)
		{
		  // ... update its path credit
		  increasePathCredit(sc, ps, pc->numTimeslots, rtscCurr);
		}
	    }
	}

      // Check again if a servable bundle has been found, whether the original or the new one
      if (((gbsBundleId > 0) && (bs->bundleCredit.value >= 0)) &&
	  ((gbsPathId == 0) || (ps->pathCredit.value >= 0) || (bs->bundleCredit.value >= overrideths)))
	{
	  
	  // DEBUG
	  /*
	    printf("t: %lu timeslot: %u - bundle %u was selected: bc %ld - path ID: %u - pc: %ld\n",
	    rtscCurr,  ss->timeslotIdx,
	    gbsBundleId, bs->bundleCredit.value,
	    gbsPathId, ps->pathCredit.value);
	  */
	  // END DEBUG
	  
	  // check the queues in the bundle (round-robin) to see which one has data to send
	  for (int i = 0; i < bc->numQueues; i++)
	    {
	      uint16_t gbsQueueId = getNextQueueToServed(bc);
	      QueueState *qs = &(ss->gbsQueue[sc->confId][gbsQueueId]);
	      uint8_t dominance = sc->streamCfg[sc->confId][gbsQueueId].dominance;   // Update stream cfg
	      
	      // ignore queue credits for BW dominated (and other) flows
	      if (dominance == STREAM_TYPE_LAT_DOMINIATE)
		{
		  increaseQueueCredit(sc, qs, bc->numTimeslots, rtscCurr);
		}
	      
	      // check if the queue can be served: it must have non-negative credits
	      if ( (dominance == STREAM_TYPE_LAT_DOMINIATE) && (qs->queueCredit.value < 0) )
		{
		  // if not, check next queue in bundle
		  continue;
		}
	      
	      // check to see if the queue has data
	      if ( !rte_ring_empty(qs->rxRing) )
		{
		  // found a non-empty queue
		  int n = rte_ring_sc_dequeue(qs->rxRing, (void **) &mbuf);
		  if (n != 0)
		    {
		      printf("Error reading from rxRing\n");
		      continue;
		    }
		  qs->nextRxRingEntry = NULL;
		  qs->nextMbufId++;
		  if (qs->nextMbufId >= TXDESC_PER_QUEUE_MAX)
		    qs->nextMbufId = 0;
		  
		  // got the packet, update credits
		  uint64_t txtimeTsc = ((mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN) * 8 * 1E6)
		    / sc->linkSpeedBpMTsc;
		  
		  // DEBUG
		  //printf("CreditUpdate: pkt_len: %u, ETHER_PHY_FRAME_PHY_OVERHEAD: %u  TELEMETRY_DATA_LEN: %u   txtimeTsc: %lu\n",
		  //     mbuf->pkt_len, ETHER_PHY_FRAME_OVERHEAD, TELEMETRY_DATA_LEN, txtimeTsc);
		  //		      printf("%lu %lu TM9 - Slot %u - Bundle %u - Credit drop: %lu for packet size %u\n",
		  //     ss->schedSeqTotal,
		  //     ss->timeslotsTotal,
		  //     ss->timeslotIdx, gbsBundleId, txtimeTsc, mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
		  //fflush(stdout);
		  // END DEBUG
		  
		  // Credit updates for served bundle, path, and queue
		  decreaseBundleCredit(sc, bs, txtimeTsc);
		  
		  if (gbsPathId > 0)
		    {
		      decreasePathCredit(sc, ps, txtimeTsc);
		    }
		  
		  if (dominance == STREAM_TYPE_LAT_DOMINIATE)
		    {
		      decreaseQueueCredit(sc, qs);
		    }
		  
		  if (likely(mbuf))
		    {
		      /// Target queue is not empty: it can be selected for GBS service
		      rtscRxDeq = RTE_RDTSC(epoch);  // slightly delayed as include CIR postponement
		      pktType = INTTYPE_GBS;
		      
		      /* Design Notes:
		       * 1. INT TLV insertion is optimized to minimize impact on TM performance.
		       *    TLV is only inserted for pkts with IPv4 dscp=DSCP_ORION_TM, which is done
		       *    by SchedRxClassifyPkt() and save the condition in mbuf.hash.usr to avoid parsing again!
		       *    It is further assumed these pkts already has TLV structure template popluated!!!
		       * 2. When testing tm3 with iperf3, need to disable INT 
		       */
		      if(rtscCurr + timeslotTsc < RTE_RDTSC(epoch))
			{
			  qs->tsViolation++;
			}
		      // CRP get rid of this for now - Keep code in case we want to capture this measurement
#if 0
		      OrionMbufUsr omu;
		      omu.usr = mbuf->hash.usr;
		      if (omu.u.addTMINT)
			{
			  char *pkt = rte_pktmbuf_mtod(mbuf, char *);
			  TMGbsTLV *tlv = get_tmgbstlv_ptr(pkt, omu.u.vlan);
			  if (tlv)
			    {
			      tlv->tmsHdr.pktType = pktType;
			      tlv->deqStates = deqStates;
			      tlv->rxQLen = rte_ring_count(qs->rxRing);  // FUTURE: fill in EqneueThread instead!
			      tlv->txQLen = qs->tsViolation;
			      
			      /* NOTE:
			       *  tscRxLatency is delay between EnqThread Classifer to start of this DeqThread's current time.
			       *  tscTxLatency is delay between this current time to TxThread's txRing dequeued time
			       */
			      tlv->tscRxLatency = (uint32_t) (rtscRxDeq - tlv->tsRtscTx);  // tlv->tsRtscTx cached as rxRtsc.
			      
			      tlv->tsRtscTx = rtscCurr;  // cache it to compute TxLatency later by TxThread
			    }
			}
#endif
		    }

            /* --- PATCH 2a : rewrite Ethernet src/dst --------------------------- */
          update_sched_mac(mbuf, sc->schedId);
		  int rval = rte_ring_sp_enqueue(ss->txRing, (void *)mbuf);
		  
		  if (likely(rval==0))
		    {
		      ss->txPktsTotal++;  // none clearing counter
		      
		      DequeueThreadStats *sps = &ss->STATS_DEQUEUE;
		      sps->txPkts++;
		      sps->txBytes += mbuf->pkt_len;
		      
		      // DEBUG: The following seems strange, because TELEMETRY_DATA_LEN is 104 bytes
		      // pkt_len is 1454 bytes, ETHER_PHY_FRAME_OVERHEAD is 24 bytes, and TELEMETRY_DATA_LEN is 104, for
		      // a total of 1582 bytes. I believe that pkt_len should be instead 1500, with 42 more bytes to be added
		      // as Ethernet overhead (18 for Ethernet header, 24 for PHY overhead). An TA measures 1494
		      // bytes per packet.
		      // AF240627: I believe it is now all taken care of.
		      //
		      //printf("Processing stats upon TX event. Total bytes counted for packet of size %u are %u\n",
		      //	 mbuf->pkt_len, mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
		      // END DEBUG
		      
		      
		      //sps->txFrameBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD);
		      sps->txSchedBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
		      sps->txGBSPkts++; 
		    }
		  else
		    {
		      ss->STATS_DEQUEUE.txRingDrops++;
		      deqStates |= DEQ_STATE_MASK_NOTSENT;
		      rte_pktmbuf_free(mbuf);
		      
		      /*
			TODO: got sent=0 so need to protect again this condition and redo scheduling 
			But do not accumulate credit??
		      */
		    }
		  
		  // NEW CONFIG CODE: AF250624 - I don't think this is needed here anymore, since it is repeated
		  // at the end of the while loop
		  // Switch config?
		  /*
		  if(sc->newConfig)
		    {
		      sc->confId = !sc->confId;
		      printf(" Switching configs!!! to %d\n", sc->confId);
		      sc->newConfig = false;
		    }
		  */
		  // END NEW CONFIG CODE
		  
		  // Idle for duration of last scheduled pkt so we don;t queue up multiple packets!!!
		  /* NOTE: Performance degraded with the static inline SchedDequeueThreadIdleWait() by <1% for both thruput
		   *       and cpu utilization at 10G input. Decided to keep the static inline function instead embedded code.
		   */
		  //SchedDequeueThreadIdleWait(ss, txtimeTsc, rtscCurr);
#if 0
		  uint64_t waitCount = 0;
		  while ( (rte_ring_count(ss->txRing) > 0) && (!forceQuit) )
		    {
		      waitCount++;
		    }
#endif
		} // end if ( !rte_ring_empty(qs->rxRing) )
	    } // end for (int i = 0; i < bc->numQueues; i++)
	      // END NEW CONFIG CODE
	} // end if ((gbsBundleId > 0) && (bs->bundleCredit.value >= 0) )
      else
	{
	  // DEBUG
	  /*
	    printf("DEBUG: candidate bundle %u was not selected: bc %ld - path ID: %u - pc: %ld\n",
	    gbsBundleId, bs->bundleCredit.value,
	    gbsPathId, ps->pathCredit.value);
	  */
	  // END DEBUG
	  
	  // DEBUG
	  /*
	    uint16_t uu;
	    for(uu = 0; uu < 41; uu++) {
	    printf("Bundle %u Credits: %ld\n", uu, ss->gbsBundle[sc->confId][uu].bundleCredit.value);
	    }
	  */
	  // END DEBUG
	}
      if (likely(pktType == PKTTYPE_UNKNOWN))
	{
	  // AF DEBUG
	  //	  printf("t: %lu slot: %u Looking for EBS queue to serve (bundleId: %u)\n", rtscCurr,  ss->timeslotIdx, gbsBundleId);
	  // END DEBUG

	  // No GBS packet selected for transmission: look for an EBS packet
	  for (int ii = (TM_NUM_CLASSES - 1); ii >= 0; ii--)
	    {
	      QueueState *qs = &(ss->ebsQueue[ii]);

	      // See if the EBS queue has data
	      if ( !rte_ring_empty(qs->rxRing) )
		{
		  // DEBUG
		  //printf("t: %lu slot: %u Found a non-empty EBS queue (%u)\n", rtscCurr,  ss->timeslotIdx, ii);
		  // END DEBUG

	  	  // Found a non-empty queue
		  int n = rte_ring_sc_dequeue(qs->rxRing, (void **) &mbuf);
		  if (n != 0)
		    {
		      printf("Error reading from rxRing\n");
		      continue;
		    }
		  qs->nextRxRingEntry = NULL;
		  qs->nextMbufId++;
		  if (qs->nextMbufId >= TXDESC_PER_QUEUE_MAX)
		    {
		      qs->nextMbufId = 0;
		    }

		  if (likely(mbuf))
		    {

		      // DEBUG
		      //printf("t: %lu slot: %u Packet of size %u found in non-empty EBS queue (%u)\n",
			     //rtscCurr,  ss->timeslotIdx, mbuf->pkt_len, ii);
		      // END DEBUG

		      // Target queue is not empty: it can be selected for EBS service
		      rtscRxDeq = RTE_RDTSC(epoch);  // slightly delayed as include CIR postponement
		      pktType = INTTYPE_EBS;
		      
		      /* Design Notes:
		       * 1. INT TLV insertion is optimized to minimize impact on TM performance.
		       *    TLV is only inserted for pkts with IPv4 dscp=DSCP_ORION_TM, which is done
		       *    by SchedRxClassifyPkt() and save the condition in mbuf.hash.usr to avoid parsing again!
		       *    It is further assumed these pkts already has TLV structure template popluated!!!
		       * 2. When testing tm3 with iperf3, need to disable INT 
		       */
		      if(rtscCurr + timeslotTsc < RTE_RDTSC(epoch))
			{
			  qs->tsViolation++;
			}
		      // CRP get rid of this for now - Keep code in case we want to capture this measurement
#if 0
		      OrionMbufUsr omu;
		      omu.usr = mbuf->hash.usr;
		      if (omu.u.addTMINT)
			{
			  char *pkt = rte_pktmbuf_mtod(mbuf, char *);
			  TMGbsTLV *tlv = get_tmgbstlv_ptr(pkt, omu.u.vlan);
			  if (tlv)
			    {
			      tlv->tmsHdr.pktType = pktType;
			      tlv->deqStates = deqStates;
			      tlv->rxQLen = rte_ring_count(qs->rxRing);  // FUTURE: fill in EqneueThread instead!
			      tlv->txQLen = qs->tsViolation;
			      
			      /* NOTE:
			       *  tscRxLatency is delay between EnqThread Classifer to start of this DeqThread's current time.
			       *  tscTxLatency is delay between this current time to TxThread's txRing dequeued time
			       */
			      tlv->tscRxLatency = (uint32_t) (rtscRxDeq - tlv->tsRtscTx);  // tlv->tsRtscTx cached as rxRtsc.
			      
			      tlv->tsRtscTx = rtscCurr;  // cache it to compute TxLatency later by TxThread
			    }
			}
#endif
		    }

             /* --- PATCH 2b : rewrite Ethernet src/dst --------------------------- */
          update_sched_mac(mbuf, sc->schedId);
		  // Enqueue the packet to the TX ring
		  int rval = rte_ring_sp_enqueue(ss->txRing, (void *)mbuf);

		  if (likely(rval==0))
		    {
		      ss->txPktsTotal++;  // none clearing counter
		      
		      DequeueThreadStats *sps = &ss->STATS_DEQUEUE;
		      sps->txPkts++;
		      sps->txBytes += mbuf->pkt_len;
		      
		      // DEBUG: The following seems strange, because TELEMETRY_DATA_LEN is 104 bytes
		      // pkt_len is 1454 bytes, ETHER_PHY_FRAME_OVERHEAD is 24 bytes, and TELEMETRY_DATA_LEN is 104, for
		      // a total of 1582 bytes. I believe that pkt_len should be instead 1500, with 42 more bytes to be added
		      // as Ethernet overhead (18 for Ethernet header, 24 for PHY overhead). An TA measures 1494
		      // bytes per packet.
		      // AF240627: I believe it is now all taken care of.
		      //
		      //printf("Processing stats upon TX event. Total bytes counted for packet of size %u are %u\n",
		      //	 mbuf->pkt_len, mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
		      // END DEBUG
		      
		      
		      //sps->txFrameBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD);
		      sps->txSchedBytes += (mbuf->pkt_len + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
		      sps->txEBSPkts++; 
		    }
		  else
		    {
		      //DBGLOG("Unexpected tx pkt cnt=%d for slot#%u,queue=%u at tsc %18"PRIu64"\n", 
		      //       sent, ss->timeslotIdx, gbsQIdx, rtscCurr);
		      ss->STATS_DEQUEUE.txRingDrops++;
		      deqStates |= DEQ_STATE_MASK_NOTSENT;
		      rte_pktmbuf_free(mbuf);
		      
		      /*
			TODO: got sent=0 so need to protect again this condition and redo scheduling 
			But do not accumulate credit??
		      */
		    }
		  
		  // Idle for duration of last scheduled pkt so we don;t queue up multiple packets!!!
		  /* NOTE: Performance degraded with the static inline SchedDequeueThreadIdleWait() by <1% for both thruput
		   *       and cpu utilization at 10G input. Decided to keep the static inline function instead embedded code.
		   */
		  //SchedDequeueThreadIdleWait(ss, txtimeTsc, rtscCurr);
#if 0
		  uint64_t waitCount = 0;
		  while ( (rte_ring_count(ss->txRing) > 0) && (!forceQuit) )
		    {
		      waitCount++;
		    }
#endif

		  if (likely(pktType == INTTYPE_EBS))
		    {
		      // Force the exit from the loop since a packet to serve was found
		      ii = 0;
		    }

		} // end if ( !rte_ring_empty(qs->rxRing) )
	    }  // end for (int ii = TM_NUM_CLASSES - 1; ii >= 0; i--)
	} // if (likely(pktType == INTPKT_UNKNOWN))
      //#endif // ADDEBSCODE
      else
	{
	  // AF DEBUG
	  //printf("t: %lu slot: %u Found GBS bundle to serve: (%u)\n", rtscCurr,  ss->timeslotIdx, gbsBundleId);
	  // END DEBUG
	}

      // Check for new configuration at the end of each loop iteration
      // Switch configuration?
      if(sc->newConfig)
	{
	  sc->confId = !sc->confId;
	  printf(" Switching PSS configuration!!! to %d\n", sc->confId);
	  sc->newConfig = false;
	}
    } // end while (!forceQuit)
}


/*
 * NOTES:
 *   rte_ring_sc_dequeue_bulk() uses RTE_RING_QUEUE_FIXED to Deq a fixed number of items from a ring
 *   rte_ring_sc_dequeue_burst() uses RTE_RING_QUEUE_VARIABLE to Deq as many items as possible from ring
 */

/* dequeue thread of rxRing traffic for pkt scheduling */
static void
SchedDequeueThread(unsigned lcoreId)
{
  RTE_LOG(INFO, SCHED, "Entering dequeue thread on lcore %u\n", lcoreId);

  unsigned sid = LcoreIdToSchedId(lcoreId);

  unsigned ret = SchedDequeueInit(lcoreId);
  if (ret != sid)
    printf("ERROR: SchedDequeueThread(): Initialization got inconsistent sid's: %u!=%u, continue anyway!\n", sid, ret);

  // RUNMODE_NORMAL below
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];

  rte_delay_ms(2000);  // IMPORTANT: sync up to prevent rxAnlyz coreedump when it had to wait for link to come up for rx driver!

  clock_gettime(CLOCK_REALTIME, &ss->todSyncStart);
  ss->tscSyncStart = rte_rdtsc();

  // Wait to begining of next scheduling sequence for ease of alignments pkt RTSC timestamp with timeslots
  {
    //rte_delay_us_block(100);  // more than 1 pkt time for this sync pkt to finish
    uint64_t rtscNow = RTE_RDTSC(ss->tscEpoch);
    uint64_t rtscEnd = rtscNow + (sc->timeslotTsc * sc->timeslotsPerSeq);
    while (rtscNow < rtscEnd) { rtscNow = RTE_RDTSC(ss->tscEpoch); }
  }  

  runConf.initMask |= INIT_MASK_DEQRUNNING;  // Inform other threads that Dequeue thread is fully running!!
  RTE_LOG(INFO, SCHED, "Dequeue thread completed init (0x%02x) on lcore %u\n", runConf.initMask, lcoreId);

  // Scheduler's main working while loop for scheduler pkt processing
  switch (sc->schedMode)
    {
    case SCHED_MODE_DCB_Q:
      SchedDequeueThreadDCB_Q(lcoreId);
      break;
#if 0
    case SCHED_MODE_SRR:
      SchedDequeueThreadSimpleRR(lcoreId);
      break;
#endif
    default:
      printf("Unknown Scheduler Algorithm %u, exiting...\n", sc->schedMode);
      forceQuit = true;
      break;
    }

  // Scheduler exited! Do some housekeeping stuff

  printf("SchedDequeueThread() exiting!\n");
}



// tx thread for tx of scheduled pkts from txRing
static void
SchedTxThread(unsigned lcoreId)
{
  unsigned sid = LcoreIdToSchedId(lcoreId);
  SchedConf  *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  uint16_t txPort = sc->txPort;

  RTE_LOG(INFO, SCHED, "entering tx thread on lcore %u\n", lcoreId);

  SchedTxThreadInit();

  uint64_t epoch = ss->tscEpoch;
  struct rte_ring *txRing = ss->txRing;

  runConf.initMask |= INIT_MASK_TXRUNNING;
  RTE_LOG(INFO, SCHED, "TX thread completed init (0x%02x) on lcore %u\n", runConf.initMask, lcoreId);

  uint64_t rtscCurr = RTE_RDTSC(epoch);
  while (!forceQuit)
    {
      struct rte_mbuf *mbuf;

      int n = rte_ring_sc_dequeue(txRing, (void **) &mbuf);
      if (n != 0)
	continue; // no pkts pending

      ss->STATS_TX.txPktsDeq++;
      // Implementation quirk: STATS updated only when there are pkts!!

      uint64_t rtscTxStart = RTE_RDTSC(epoch);
      uint64_t tscIdleWait = rtscTxStart - rtscCurr;
      ss->STATS_TX.tscTxLcoreIdle += tscIdleWait;
      uint16_t pktlen = mbuf->pkt_len;  // cache as mbuf is asynchronously freed by tx driver
      // Transmit
      while (!forceQuit)
	{
	  int sent = rte_eth_tx_burst(txPort, ss->txqId, &mbuf, 1);
	  if (likely(sent == 1))
	    break;
	}

      // DEBUG
      // Only difference between TM8 and TM9: see what happens if I take off the next line!
      //rte_pktmbuf_free(mbuf);
      // END DEBUG

      
      ss->STATS_TX.txPktsSent++;      
      ss->STATS_TX.txBytes += pktlen;
      //ss->STATS_TX.txFrameBytes += (pktlen + ETHER_PHY_FRAME_OVERHEAD);
      ss->STATS_TX.txSchedBytes += (pktlen + ETHER_PHY_FRAME_OVERHEAD + TELEMETRY_DATA_LEN);
      uint64_t tscTx = RTE_RDTSC(epoch) - rtscTxStart;
      ss->STATS_TX.tscTxLcoreBusy += tscTx;

      rtscCurr = RTE_RDTSC(epoch);

    }

  printf("SchedTxThread() exiting!\n");
}

static void
SchedMainThread(unsigned lcoreId)
{
  struct stat file_stat;
  unsigned timerSec;
  int first = 0;
  bool freeOldBuffer = false;
  uint8_t thisConfig = 0;

  timerSec = runConf.statsTimerSec;
  RTE_LOG(INFO, SCHED, "entering main loop on lcore %u, stats interval = %u seconds\n", lcoreId, timerSec);

  unsigned sid = LcoreIdToSchedId(lcoreId);
  SchedConf *sc = &schedConf[sid];
  SchedState *ss = &schedState[sid];
  
  uint64_t tsc0=rte_rdtsc();
  uint64_t tscHz = rte_get_tsc_hz();
  uint64_t rtscNextPrint=0;  // Force an initial print

  rte_delay_us(100*USEC_PER_MSEC);

  while (!forceQuit)
    {
      static uint32_t prints=0;
      if (timerSec > 0)
	{
	  rte_delay_us(USEC_PER_MSEC);
	  uint64_t rtscNow = RTE_RDTSC(tsc0);
	  if (rtscNow >= rtscNextPrint)
	    {
	      // free rte_mbuf(confId))
	      if (freeOldBuffer) {
		for(int sidx; sidx <= sc->numStreams; sidx++) {
		  struct rte_mbuf *mbuf = ss->streamPktMbuf[thisConfig][sidx];
		  rte_pktmbuf_free(mbuf);
		}
		freeOldBuffer = false;
	      }

	      //#if 0
	      uint64_t drops=0;
	      SummaryEnqueueStatsPrint(sid, &schedState[0], prints*timerSec, &drops);
	      SummaryDequeueStatsPrint(sid, &schedState[0], prints*timerSec, &drops);
	      SummaryTxStatsPrint(sid, &schedState[0], prints*timerSec);
	      SummaryEtherPortStatsPrint(sc->txPort, prints*timerSec, &drops);
	      printf("Total Scheduler Pkt Drops %12"PRIu64"\n", drops);
	      rtscNextPrint = rtscNow + timerSec*tscHz;  // may accumulate error inlieu of "rtscNextPrint+=timerSec*tscHz"
	      prints++;
	      //#endif
	      //}
	      //}
	      // Config Update code
	      // Determine if config file was updated
	      rtscNextPrint = rtscNow + timerSec*tscHz;  // may accumulate error inlieu of "rtscNextPrint+=timerSec*tscHz"
	      int err = stat(sc->schedCfgFile, &file_stat);
	      if(err != 0) {
		printf("Error getting file stat in file_is_modified ");
		//return false;
		continue;
	      }
	      if (first == 0){
		sc->lastUpdateTime = file_stat.st_mtime;
		printf(" FIRST CHECK on cfg file\n");
		first++;

		// AF DEBUG
		/*
		uint16_t ii;
		printf("BUNDLE CREDIT INITIALIZATION VALUES\n");
		for(ii = 0; ii < 41; ii++) {
		  printf("Bundle %u  Credit: %ld\n", ii, ss->gbsBundle[thisConfig][ii].bundleCredit.value);
		}
		*/
		// END DEBUG

	      }
	      if(file_stat.st_mtime > sc->lastUpdateTime) {
		// Found a new verson of the scheduler configuraton file: swap configuration
		printf(" UPDATE ");
		int confId = !sc->confId; 
		// Reset configurations and state for this confId
		memset(&sc->pss[confId][0], 0, sizeof(sc->pss)/2);
		memset(&sc->bundleConf[confId][0], 0, sizeof(sc->bundleConf)/2);
		memset(&ss->gbsBundle[confId][0], 0, sizeof(ss->gbsBundle)/2);
		memset(&ss->gbsQueue[confId][0], 0, sizeof(ss->gbsQueue)/2);
		QueueState *qs;
		for (int q=0; q<sc->queuesNum; q++)
		  {
		    qs = &ss->gbsQueue[confId][q];
		    //memset(qs, 0, sizeof(QueueState));  // Not required as done by SchedState init
		    qs->qtype = QUEUE_TYPE_GBS;
		    qs->qid = (uint16_t) q & 0xffff;
		  }
    
		// Parse the new configuraton file for the scheduler
		int ret = app_parse_scf(sc->schedId, sc->schedCfgFile, confId);
		if (ret != 0) {
		  // failed ... ignore
		  printf("Failure to parse updated config file %s \n", sc->schedCfgFile);
		}
		else { // TM config successful

		  // AF250521: There is no stream configuration file with TM9: should this
		  // entire piece of code be removed?

		  // Assume stream file also changed.  Use configId that was set above
		  // Reset Configs and state ... may do it along with other above
		  memset(&sc->streamCfg[confId][0], 0, sizeof(sc->streamCfg)/2);
		  sc->streamsBaseNum = 0;
		  sc->numStreams = 0;
		  ret = app_parse_strmcf(sc->schedId, sc->streamCfgFile, confId);
		  if(ret !=0) {
		    // failed ... ignore
		    printf("Failure to parse updated stream file %s \n", sc->schedCfgFile);
		  }
		  else {
		    // now initialize packets
		    if(StreamPktInit(confId, 0) < 0){
		      printf("Failure to n stream packet init");
		    }
		    else {
		      StreamRatesValidate(sc, confId);
		      sc->newConfig = true;
		      freeOldBuffer = true;
		      thisConfig = sc->confId; // frees old config buffers
		    }
		  }
		}
		// Update file time either way
		sc->lastUpdateTime = file_stat.st_mtime;
	      }
	      //  End config update code
	    }
	}
    }

  printf("SchedMainThread() exiting!\n");
}


void
SchedThreadsDispatcher(void)
{
  static bool nonce=false;

  if (nonce==false)
    {
      timerPeriodTsc = timerPeriodSec * rte_get_timer_hz();
      nonce = true;
    }

  unsigned lcoreId = rte_lcore_id();
  unsigned sid = LcoreIdToSchedId(lcoreId);
  SchedConf *sc = &schedConf[sid];

  /* PoC Phase 3 TM design supports 1 Input Port (Network intf or SRIOV for VMs) and 1 Output port.
   * Threads:
   * - SchedEnqueueThread(), SchedDequeueThread() and SchedTxThread() for TM scheduled traffic. In Phase1, we
   *   support single direction of traffic admission control of pkts from Network Edge (Gateway input or SRIOV).
   * - SchedL2fwdThread() supports reverse direction traffic w/o a TM
   * - SchedMainThread() - thread monitoring and statistics reporting
   */

  if (lcoreId == sc->rxCore)
    SchedEnqueueThread(lcoreId);
  else if (lcoreId == sc->tmCore)
    SchedDequeueThread(lcoreId);
  else if (lcoreId == sc->txCore)
    SchedTxThread(lcoreId);
  else if (lcoreId == rte_get_main_lcore())
    SchedMainThread(lcoreId);
  else
    {
      rte_exit(EXIT_FAILURE, "ERROR: lcore %u has unknown role!\n", lcoreId);
    }

  // Falls thru when any of the TM threads exited!

  //  forceQuit = true;  // force other threads to quit also!
  printf("sched_thread_dispatcher returning for lcore %u!\n", lcoreId);
}
