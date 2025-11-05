/* tmEthdev.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "../common/OrionDpdk.h"

#define	NUM_SCHED_LCORES	1
#define MAX_PKT_BURST		32
#define MEMPOOL_CACHE_SIZE	256

#define	SCHED_MAX_ETHPORTS	8	// RTE_MAX_ETHPORTS is 32

struct rte_mempool *pktmbufPool = NULL;
static struct rte_mempool *pktmbufPoolRxPort[SCHED_MAX_ETHPORTS];
static struct rte_ether_addr portEtherAddr[SCHED_MAX_ETHPORTS];


// Configurable number of RX/TX ring descriptors
#define RTE_TEST_RX_DESC_DEFAULT 4096
static uint16_t numRxDesc = RTE_TEST_RX_DESC_DEFAULT;
static uint16_t numTxDesc;

static inline uint16_t
RoundUpToPowerOf2(uint16_t size)
{
  if (size > 32768)
    {
      printf("WARNING: size limited to 32768\n");
      return 32768;
    }
  if (size > 16384) return 32768;
  if (size > 8192) return 16384;
  else if (size > 4096) return 8192;
  else if (size > 2048) return 4096;
  else if (size > 1024) return 2048;
  else if (size > 512) return 1024;
  else if (size > 256) return 512;
  else if (size > 128) return 256;
  else if (size > 64) return 128;
  else if (size > 32) return 64;
  else if (size > 16) return 32;
  else if (size > 8) return 16;
  else if (size > 4) return 8;
  else if (size > 2) return 4;
  else if (size > 1) return 2;
  return 1;
}

static inline uint16_t
RoundDownToPowerOf2(uint16_t size)
{
  if (size < 2) return 1;
  if (size < 4) return 2;
  if (size < 8) return 4;
  if (size < 16) return 8;
  if (size < 32) return 16;
  if (size < 64) return 32;
  if (size < 128) return 64;
  if (size < 256) return 128;
  if (size < 512) return 256;
  if (size < 1024) return 512;
  if (size < 2048) return 1024;
  if (size < 4096) return 2048;
  if (size < 8192) return 4096;
  if (size < 8192) return 4096;
  if (size < 16384) return 8192;
  if (size < 32768) return 16384;
  if (size == 32768) return 32768;

  printf("WARNING: size limited to 32768\n");
  return 32768;
}

double
tscClockCalibrate(bool verbose)
{
  struct timespec todStart, todEnd;
  uint64_t tscStart, tscEnd;
  uint64_t nsecDelta, tscDelta;
  double tscHz;

  clock_gettime(CLOCK_MONOTONIC, &todStart);
  tscStart = rte_rdtsc();

  rte_delay_ms(1000);

  clock_gettime(CLOCK_MONOTONIC, &todEnd);
  tscEnd = rte_rdtsc();

  nsecDelta = ((uint64_t)todEnd.tv_sec * NSEC_PER_SEC + todEnd.tv_nsec) - ((uint64_t)todStart.tv_sec * NSEC_PER_SEC + todStart.tv_nsec);
  tscDelta  = tscEnd - tscStart;
  tscHz = ((double)tscDelta * NSEC_PER_SEC) / (double)nsecDelta;

  if (verbose)
    printf("TSC Calibration: Elapsed  tod=%"PRIu64"nsec, tsc=%"PRIu64"  TSC_HZ=%f\n", nsecDelta, tscDelta, tscHz);

  return tscHz;
}

static struct rte_eth_conf portConf = {
  // 	.rxmode = {
  // 		.split_hdr_size = 0, removed in 22.11
  //	},
  .txmode = {
    .mq_mode = RTE_ETH_MQ_TX_NONE,
  },
};

/*
 * Check the link status of all ports in up to maxSeconds, and print them finally
 * Returns: 0 if all up, -1 if timeout -2 if forceQuit
 */
int
ethdev_wait_all_ports_up(uint32_t portsMask, int maxSeconds)
{
#define CHECK_INTERVAL 100 /* 100ms */
  uint16_t portId;
  bool allPortsUp=false, printFlag = false;
  int count, maxCnt;
  struct rte_eth_link link;

  printf("\nChecking link status...");
  fflush(stdout);
  maxCnt = (maxSeconds * 1000)/CHECK_INTERVAL;

  for (count = 0; count <= maxCnt; count++)
    {
      if (forceQuit)
	return -2;
      allPortsUp = true;
      RTE_ETH_FOREACH_DEV(portId) {
	if (forceQuit)
	  return -2;
	if ((portsMask & (1 << portId)) == 0)
	  continue;
	memset(&link, 0, sizeof(link));
	rte_eth_link_get_nowait(portId, &link);
	/* print link status if flag set */
	if (printFlag == true) {
	  if (link.link_status)
	    {
	      printf(
		     "Port%d Link Up. Speed %u Mbps - %s\n",
		     portId, link.link_speed,
		     (link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX) ?
		     ("full-duplex") : ("half-duplex\n"));
	    }
	  else
	    printf("Port %d Link Down\n", portId);
	  continue;
	}
	/* clear allPortsUp flag if any link down */
	if (link.link_status == RTE_ETH_LINK_DOWN) {
	  allPortsUp = false;
	  break;
	}
      }
      /* after finally printing all link status, get out */
      if (printFlag == true)
	break;

      if (allPortsUp == false) {
	printf(".");
	fflush(stdout);
	rte_delay_ms(CHECK_INTERVAL);
      }

      /* set the printFlag if all ports up or timeout */
      if (allPortsUp == true || count == (maxCnt - 1)) {
	printFlag = true;
	printf("done\n");
      }
    }

  return (allPortsUp==true) ? 0 : -1;
}

/* SCHED_TM: This function is similar to DPDK_TM:init.c:app_init_port()
 *    Differences: DPDK_TM has custom rx_conf and tx_conf threshold settings.
 *
 * NOTE: Only a single txqId specified to schedule all SCHED pkts despite txqNum value!!!
 *
 */
int
ethdev_init(uint32_t cpuSocket, uint32_t portsMask, RunConf *rc)
{
  uint32_t portId, numPorts;
  uint32_t numPortsAvail=0;

  numPorts = rte_eth_dev_count_avail();
  if (numPorts == 0)
    rte_exit(EXIT_FAILURE, "No Ethernet ports - bye\n");

  /* check port mask to possible port mask */
  if (portsMask & ~((1 << numPorts) - 1))
    rte_exit(EXIT_FAILURE, "Invalid portmask; possible (0x%x)\n",
	     (1 << numPorts) - 1);

  // AF250520: maximize the value of numTxDesc, given that it is a uint16_t
  //numTxDesc = 32768;

  // AF251027: reduce numTxDesc for alignment requirements in ConnectX-4 Lx
  numTxDesc = 16384;
  
  /*
  numTxDesc = (TXDESC_PER_QUEUE_MAX * NUM_GBSQUEUES_MAX * NUM_SCHED_MAX) + 4096; // Add 4K margin
  numTxDesc = RoundUpToPowerOf2(numTxDesc);
  */

  // int numMbuffs = numTxDesc + numRxDesc;
  int numMbuffs = 400000;

  /* create the mbuf pool. The RTE_MBUF_DEFAULT_BUF_SIZE is defined to accomodate 2048 bytes pkt. */
  // pktmbufPool = rte_pktmbuf_pool_create("MbufPool", numMbuffs, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, cpuSocket);
  pktmbufPool = rte_pktmbuf_pool_create("MbufPool", 1024, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, cpuSocket);
  if (pktmbufPool == NULL)
    rte_exit(EXIT_FAILURE, "Cannot init mbuf pool for locally generated traffic\n");

  /* Initialise each port */
  RTE_ETH_FOREACH_DEV(portId) {
    struct rte_eth_rxconf rxqConf;
    struct rte_eth_txconf txqConf;
    struct rte_eth_conf portConfLocal = portConf;
    struct rte_eth_dev_info devInfo;

    printf("Checking portId%u with portsMask = 0x%x\n", portId, portsMask);

    /* skip ports that are not enabled */
    if ((portsMask & (1 << portId)) == 0) {
      printf("Skipping disabled port %u\n", portId);
      continue;
    }
    numPortsAvail++;

    if (portId >= SCHED_MAX_ETHPORTS)
      rte_exit(EXIT_FAILURE, "portId %u exceeded SCHED_MAX_ETHPORTS=%u - bye\n", portId, SCHED_MAX_ETHPORTS);

    /* init port: */
    printf("Initializing port %u...\n", portId);
    fflush(stdout);

    rte_eth_dev_info_get(portId, &devInfo);
    //printf("DBG: rx_offloads_capability=0x%lx, tx_offloads_capability=0x%lx!\n", devInfo.rx_offload_capa, devInfo.tx_offload_capa);
    //printf("DBG: max_rx_queues=%u, max_tx_queues=%u!\n", (unsigned)devInfo.max_rx_queues, (unsigned)devInfo.max_tx_queues);
    //printf("DBG: max_rx_desc=%u, max_tx_desc=%u!\n", (unsigned)devInfo.rx_desc_lim.nb_max, (unsigned)devInfo.tx_desc_lim.nb_max);

    if (rc->txqNum > devInfo.max_tx_queues)
      {
	rte_exit(EXIT_FAILURE, "Invalid port%u txq config range: txqNum=%u\n", portId, rc->txqNum);
      }
    if (devInfo.rx_offload_capa & RTE_ETH_RX_OFFLOAD_TIMESTAMP)
      {
	portConfLocal.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TIMESTAMP;
	//printf("DBG: rx_offloads=0x%lx with DEV_RX_OFFLOAD_TIMESTAMP enabled\n", portConfLocal.rxmode.offloads);
      }
    else
      {
	//printf("DBG: rx_offloads_capability=0x%lx DEV_RX_OFFLOAD_TIMESTAMP not supported!\n", devInfo.rx_offload_capa);
      }
    if (devInfo.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
      {
#undef OFFLOAD_MBUF_DISABLE	// 10/23/20: Tried on ensap4a with 10G and trafgen @10Gbps. No difference in performance as imissed drops remained same! 
#ifdef OFFLOAD_MBUF_DISABLE
	printf("TESTING: tx_offloads=0x%lx, DEV_TX_OFFLOAD_MBUF_FAST_FREE supported but not enabled!\n", portConfLocal.txmode.offloads);
#else
	portConfLocal.txmode.offloads |= RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;
	//printf("DBG: tx_offloads=0x%lx with DEV_TX_OFFLOAD_MBUF_FAST_FREE enabled\n", portConfLocal.txmode.offloads);
#endif
      }
    else
      {
	//printf("DBG: tx_offloads_capability=0x%lx DEV_TX_OFFLOAD_MBUF_FAST_FREE not supported!\n", devInfo.tx_offload_capa);
      }

    int ret = rte_eth_dev_configure(portId, rc->rxqNum, rc->txqNum, &portConfLocal);	// 1 RxQ and specified number of scheduler TxQ's
    //int ret = rte_eth_dev_configure(portId, 0, rc->txqNum, &portConfLocal);	// 1 RxQ and specified number of scheduler TxQ's
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "Cannot configure device: err=%d, port=%u\n", ret, portId);

    ret = rte_eth_dev_adjust_nb_rx_tx_desc(portId, &numRxDesc, &numTxDesc);	// modifies number of RxDesc and TxDesc
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "Cannot adjust number of descriptors: err=%d, port=%u\n", ret, portId);

    //printf("DBG: Port#%u, numRxDesc=%u, numTxDesc=%u, numMbuffs=%u\n", portId, numRxDesc, numTxDesc, numMbuffs);

    rte_eth_macaddr_get(portId,&portEtherAddr[portId]);

    char mbufName[32];
    snprintf(mbufName, 30, "MbufPoolRxPort%u", portId);
    pktmbufPoolRxPort[portId] = rte_pktmbuf_pool_create(mbufName, numMbuffs, MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, cpuSocket);
    if (pktmbufPoolRxPort[portId] == NULL)
      rte_exit(EXIT_FAILURE, "Cannot init mbuf pool for port#%u\n", portId);

    /* init one RX queue, not used! */
    fflush(stdout);
    rxqConf = devInfo.default_rxconf;
    rxqConf.offloads = portConfLocal.rxmode.offloads;

    // Each Rx Queue has physically separated mbuf space by DPDK driver!
    uint16_t numRxDescPerRxQ = RoundDownToPowerOf2(numRxDesc / rc->rxqNum);	// Derive the numRxDescPerRxQ from total numRxDesc 
    //uint16_t numRxDescPerRxQ = RoundDownToPowerOf2(numRxDesc / 1);	// Derive the numRxDescPerRxQ from total numRxDesc 

    //printf("DBG: Total Rx descriptors=%u, numRxDescPerRxQ=%u for %u rxQueues!\n", numRxDesc, numRxDescPerRxQ, rc->rxqNum);
    printf("DBG: Total Rx descriptors=%u, numRxDescPerRxQ=%u for %u rxQueues!\n", numRxDesc, numRxDescPerRxQ, 1);
    for (uint16_t q=0; q<rc->rxqNum; q++)
      {
	ret = rte_eth_rx_queue_setup(portId, q, numRxDescPerRxQ, rte_eth_dev_socket_id(portId), &rxqConf, pktmbufPoolRxPort[portId]);
	if (ret < 0)
	  {
	    rte_exit(EXIT_FAILURE, "rte_eth_rx_queue_setup:err=%d, port=%u, queue%u\n", ret, portId, (unsigned)q);
	  }
	printf("DBG: rte_eth_rx_queue_setup(port%u, q%u, rxDesc=%u) completed!\n", portId, q, numRxDescPerRxQ);
      }

    /* init one TX queue on each port. */
    fflush(stdout);
    txqConf = devInfo.default_txconf;
    txqConf.offloads = portConfLocal.txmode.offloads;
    ret = rte_eth_tx_queue_setup(portId, rc->txqId, numTxDesc, rte_eth_dev_socket_id(portId), &txqConf);
    if (ret < 0)
      {
	rte_exit(EXIT_FAILURE, "rte_eth_tx_queue_setup:err=%d, port=%u, txq%u\n", ret, portId, rc->txqId);
      }

#if 0
    if (rc->flowIsolate)
      {
	ret = rte_flow_isolate(portId, 1, NULL);
	if (ret == 0)
	  printf("DBG: port%u flow isolate mode enabled to support bifurcation\n", portId);
	else
	  rte_exit(EXIT_FAILURE, "Port%u flow isolate configuration failed\n", portId);
      }
    else
#endif
      {
	// flowIsolate preclude promiscuous config per https://doc.dpdk.org/guides/prog_guide/rte_flow.html#flow-isolated-mode
	if (rc->promiscuous)
	  ret = rte_eth_promiscuous_enable(portId);
	else
	  ret = rte_eth_promiscuous_disable(portId);
	if (ret == 0)
	  printf("Port%u promiscuous mode %s\n", portId, (rc->promiscuous==true)?"enabled":"disabled");
	else
	  rte_exit(EXIT_FAILURE, "Port%u promiscuous configuration failed\n", portId);
      }

    /* Start device */
    ret = rte_eth_dev_start(portId);
    if (ret < 0)
      rte_exit(EXIT_FAILURE, "rte_eth_dev_start:err=%d, port=%u\n", ret, portId);

    printf("Port %u, MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n\n",
	   portId,
	   portEtherAddr[portId].addr_bytes[0],
	   portEtherAddr[portId].addr_bytes[1],
	   portEtherAddr[portId].addr_bytes[2],
	   portEtherAddr[portId].addr_bytes[3],
	   portEtherAddr[portId].addr_bytes[4],
	   portEtherAddr[portId].addr_bytes[5]);
  }

  if (!numPortsAvail) {
    rte_exit(EXIT_FAILURE, "All available ports are disabled. Please set portmask.\n");
  }

  return 0;
}

