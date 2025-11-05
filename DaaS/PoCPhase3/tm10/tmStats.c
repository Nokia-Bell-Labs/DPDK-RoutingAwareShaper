/* tmStats.c 
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmStats.h"

#define BITS_PER_GBPS 1.0e9

/* Print Dequeue Thread statistics */
void
SummaryDequeueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops)
{
	static DequeueThreadStats deqPrev;
	static uint32_t secsPrev;
	DequeueThreadStats deqDelta, deqNew;
	rte_memcpy(&deqNew, &ssp->STATS_DEQUEUE, sizeof(DequeueThreadStats));

	deqDelta.timeslots           = deqNew.timeslots           - deqPrev.timeslots;
	deqDelta.timeslotsSkipped    = deqNew.timeslotsSkipped    - deqPrev.timeslotsSkipped;
	deqDelta.schedSequences      = deqNew.schedSequences      - deqPrev.schedSequences;
	//deqDelta.schedSequencesMulti  = deqNew.schedSequencesMulti - deqPrev.schedSequencesMulti;
	deqDelta.txPkts              = deqNew.txPkts              - deqPrev.txPkts;
	deqDelta.txBytes             = deqNew.txBytes             - deqPrev.txBytes;
	//deqDelta.txFrameBytes        = deqNew.txFrameBytes        - deqPrev.txFrameBytes;
	deqDelta.txSchedBytes        = deqNew.txSchedBytes        - deqPrev.txSchedBytes;
	deqDelta.txGBSPkts           = deqNew.txGBSPkts           - deqPrev.txGBSPkts;
	deqDelta.txEBSPkts           = deqNew.txEBSPkts           - deqPrev.txEBSPkts;
	deqDelta.txSyncPkts          = deqNew.txSyncPkts          - deqPrev.txSyncPkts;
	deqDelta.tscSchedErrMax      = deqNew.tscSchedErrMax;
	deqDelta.tscSchedErrExc      = deqNew.tscSchedErrExc      - deqPrev.tscSchedErrExc;
	deqDelta.tscDeqLcoreBusy     = deqNew.tscDeqLcoreBusy     - deqPrev.tscDeqLcoreBusy;
	deqDelta.tscDeqLcoreIdle     = deqNew.tscDeqLcoreIdle     - deqPrev.tscDeqLcoreIdle;
	deqDelta.txRingDrops         = deqNew.txRingDrops         - deqPrev.txRingDrops;
	*drops += deqDelta.txRingDrops;

	rte_memcpy(&deqPrev, &deqNew, sizeof(DequeueThreadStats));	// save new previous values

	uint64_t nsecSchedErrMax = (NSEC_PER_SEC * deqDelta.tscSchedErrMax) / rte_get_tsc_hz();

	printf("\nDequeueStatistics for TM%u  %usec ------------------------------"
		   "\nTx Sequence/Slots/Skipped/Max:  %12u/%12u/%12u/%12u"
		   //"\nTx Sequence/Slots/Multi:        %12u/%12u/%12u"
		   "\nTx pkts/bytes/sched bytes:      %12"PRIu64"/%12"PRIu64"/%12"PRIu64
		   "\nTx rate/scheduling rate:        %8.4fG/%8.4fG"
		   "\nTx pkts GBS/EBS/Sync:           %12"PRIu64"/%12"PRIu64"/%12"PRIu64
		   "\nTx ringDrops:                   %12u"
		   "\ntscSchedErr max/usec/Exc:       %12"PRIu64"/%12"PRIu64"/%12"PRIu64
		   "\nDeq Busy/Idle/BusyPct:          %12"PRIu64"/%12"PRIu64"/%8.4f%%",
		   schedId, secs,
	           deqDelta.schedSequences,
	           //deqDelta.schedSequencesMulti,
	           deqDelta.timeslots,
	           deqDelta.timeslotsSkipped,
	           deqNew.timeslotsSkippedMax,
	           deqDelta.txPkts,
	           deqDelta.txBytes,
	           deqDelta.txSchedBytes,
	           (float)(deqDelta.txBytes * 8)/((float)(secs - secsPrev) * BITS_PER_GBPS),
	           (float)(deqDelta.txSchedBytes * 8)/((float)(secs - secsPrev) * BITS_PER_GBPS),
	           deqDelta.txGBSPkts,
	           deqDelta.txEBSPkts,
	           deqDelta.txSyncPkts,
		   deqDelta.txRingDrops,
	           deqDelta.tscSchedErrMax,
	           nsecSchedErrMax,
	           deqDelta.tscSchedErrExc,
	           deqDelta.tscDeqLcoreBusy,
	           deqDelta.tscDeqLcoreIdle,
		   (float)(deqDelta.tscDeqLcoreBusy * 100)/(float)(deqDelta.tscDeqLcoreBusy + deqDelta.tscDeqLcoreIdle)
	       );

	if (ssp->txRing)  // check of null to avoid race condition with Tx thread
	{
		int capacity = rte_ring_get_capacity(ssp->txRing);  // Should equal APP_RING_SIZE
		printf("\nTxRing size=%d, occupany: %4x", capacity, rte_ring_count(ssp->txRing));
	}

	printf("\n====================================================\n");
	secsPrev = secs;
        ssp->STATS_DEQUEUE.timeslotsSkippedMax = 0;
}

/* Print Tx Thread statistics */
void
SummaryTxStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs)
{
	static TxThreadStats txPrev;
	static uint32_t secsPrev;
	TxThreadStats txDelta, txNew;
	rte_memcpy(&txNew, &ssp->STATS_TX, sizeof(TxThreadStats));

	txDelta.txPktsDeq       = txNew.txPktsDeq       - txPrev.txPktsDeq;
	txDelta.txPktsSent      = txNew.txPktsSent      - txPrev.txPktsSent;
	txDelta.txBytes         = txNew.txBytes         - txPrev.txBytes;
	//txDelta.txFrameBytes    = txNew.txFrameBytes    - txPrev.txFrameBytes;
	txDelta.txSchedBytes    = txNew.txSchedBytes    - txPrev.txSchedBytes;
	txDelta.tscTxLcoreBusy  = txNew.tscTxLcoreBusy  - txPrev.tscTxLcoreBusy;
	txDelta.tscTxLcoreIdle  = txNew.tscTxLcoreIdle  - txPrev.tscTxLcoreIdle;

	rte_memcpy(&txPrev, &txNew, sizeof(TxThreadStats));	// save new previous values

	printf("\nTxStatistics for TM%u  %usec ------------------------------"
		   "\nTx pkts deq:               %12"PRIu64
		   "\nTx pkts/bytes/sched bytes: %12"PRIu64"/%12"PRIu64"/%12"PRIu64
		   "\nTx rate/scheduling rate:   %8.4fG/%8.4fG"
		   "\nTx busy/idle/busy pct:     %12"PRIu64"/%12"PRIu64"/%8.4f%%",
		   schedId, secs,
	           txDelta.txPktsDeq,
	           txDelta.txPktsSent,
	           txDelta.txBytes,
	           txDelta.txSchedBytes,
	           (float)(txDelta.txBytes * 8)/((float)(secs - secsPrev) * BITS_PER_GBPS),
	           (float)(txDelta.txSchedBytes * 8)/((float)(secs - secsPrev) * BITS_PER_GBPS),
	           txDelta.tscTxLcoreBusy,
	           txDelta.tscTxLcoreIdle,
		   (float)(txDelta.tscTxLcoreBusy * 100)/(float)(txDelta.tscTxLcoreBusy + txDelta.tscTxLcoreIdle)
	       );

	printf("\n====================================================\n");
	secsPrev = secs;
}

void
SummaryEtherPortStatsPrint(unsigned portId, uint32_t secs, uint64_t *drops)
{
	static uint32_t secsPrevTbl[NUM_PORTSPERSCHED_MAX];
	static struct rte_eth_stats dpdkPortStatsPrevTbl[NUM_PORTSPERSCHED_MAX];

	uint32_t             *secsPrev  = &secsPrevTbl[portId];
	struct rte_eth_stats *statsPrev = &dpdkPortStatsPrevTbl[portId];

	// NOTE: Besides the API documented Pkt/Bytes counters, the i40e_dev_stats_get() supports imissed but not rx_nombuf.
	struct rte_eth_stats dpdkPortStats, delta;
	int ret = rte_eth_stats_get(portId, &dpdkPortStats);
	if (ret==0)
	{
		delta.ipackets = dpdkPortStats.ipackets - statsPrev->ipackets;
		delta.imissed  = dpdkPortStats.imissed  - statsPrev->imissed;
		// Not supported by i40e:  delta.rx_nombuf= dpdkPortStats.rx_nombuf - statsPrev->rx_nombuf;
		delta.opackets = dpdkPortStats.opackets - statsPrev->opackets;
		delta.ibytes   = dpdkPortStats.ibytes   - statsPrev->ibytes;
		delta.obytes   = dpdkPortStats.obytes   - statsPrev->obytes;
		delta.ierrors  = dpdkPortStats.ierrors  - statsPrev->ierrors;
		delta.oerrors  = dpdkPortStats.oerrors  - statsPrev->oerrors;
		//uint64_t ibytes_framing = delta.ibytes + (delta.ipackets * ETHER_PHY_FRAME_OVERHEAD);
		//uint64_t obytes_framing = delta.obytes + (delta.opackets * ETHER_PHY_FRAME_OVERHEAD);
		uint64_t ibytes_framing = delta.ibytes + (delta.ipackets * ETHER_DL_FRAME_OVERHEAD);
		uint64_t obytes_framing = delta.obytes + (delta.opackets * ETHER_DL_FRAME_OVERHEAD);

		*drops += delta.imissed;

		printf("\nDPDK port#%u Statistics (May be incorrect!) --------"
		   "\nRx pkts/bytes/errs/+framing/gbps:  %12"PRIu64"/%12"PRIu64"/%12"PRIu64"/%12"PRIu64"/%8.4fG"
		   "\nRx missed:                         %12"PRIu64
		   "\nTx pkts/bytes/errs/+framing/gbps:  %12"PRIu64"/%12"PRIu64"/%12"PRIu64"/%12"PRIu64"/%8.4fG",
		   portId,
		   delta.ipackets,
		   delta.ibytes,
		   delta.ierrors,
		   ibytes_framing,
		   (float)(ibytes_framing * 8)/(float)((secs - *secsPrev) * BITS_PER_GBPS),
		   delta.imissed,
		   delta.opackets,
		   delta.obytes,
		   delta.oerrors,
		   obytes_framing,
		   (float)(obytes_framing * 8)/(float)((secs - *secsPrev) * BITS_PER_GBPS)
		);

		rte_memcpy(statsPrev, &dpdkPortStats, sizeof(struct rte_eth_stats));
	}

	printf("\n====================================================\n");
	*secsPrev = secs;
}

/* Print Enqueue Thread statistics */
void
SummaryEnqueueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops)
{
	static EnqueueThreadStats enqPrev;
	static uint32_t secsPrev;
	EnqueueThreadStats enqDelta, enqNew;
	rte_memcpy(&enqNew, &ssp->STATS_ENQUEUE, sizeof(EnqueueThreadStats));

	for (unsigned q=0; q<runConf.rxqNum; q++)
		enqDelta.rxqPkts[q] = enqNew.rxqPkts[q] - enqPrev.rxqPkts[q];

	enqDelta.rxPkts          = enqNew.rxPkts          - enqPrev.rxPkts;
	enqDelta.rxBytes         = enqNew.rxBytes         - enqPrev.rxBytes;
	enqDelta.rxFrameBytes    = enqNew.rxFrameBytes    - enqPrev.rxFrameBytes;
	enqDelta.rxRingDrops     = enqNew.rxRingDrops     - enqPrev.rxRingDrops;
	enqDelta.tscEnqLcoreBusy  = enqNew.tscEnqLcoreBusy  - enqPrev.tscEnqLcoreBusy;
	enqDelta.tscEnqLcoreIdle  = enqNew.tscEnqLcoreIdle  - enqPrev.tscEnqLcoreIdle;
	*drops += enqDelta.rxRingDrops;

	rte_memcpy(&enqPrev, &enqNew, sizeof(EnqueueThreadStats));	// save new previous values

	printf("\nEnqueueStatistics for TM%u  %usec ------------------------------", schedId, secs);

	printf("\nrxQPkts:"); 
	for (uint16_t q=0; q<runConf.rxqNum; q++)
		printf(" %12"PRIu64, enqDelta.rxqPkts[q]);

	unsigned avgPktsize=0;
	if (likely(enqDelta.rxPkts>0))
	{
		avgPktsize = (unsigned) (enqDelta.rxBytes / enqDelta.rxPkts);
		avgPktsize = (unsigned) ((enqDelta.rxBytes + avgPktsize/2) / enqDelta.rxPkts);	// compute again with roundoff
	}

	printf(
		   "\nRx pkts/bytes/+framing/gbps:  %12"PRIu64"/%12"PRIu64"/%12"PRIu64"/%8.4fG, avg_pktsize=%u"
		   "\nRx ringDrops:                 %12"PRIu64
		   "\nEnq Busy/Idle/BusyPct:        %12"PRIu64"/%12"PRIu64"/%8.4f%%",
	           enqDelta.rxPkts,
	           enqDelta.rxBytes,
	           enqDelta.rxFrameBytes,
	           (float)(enqDelta.rxBytes * 8)/(float)((secs - secsPrev) * BITS_PER_GBPS),
	           //(float)(enqDelta.rxFrameBytes * 8)/(float)((secs - secsPrev) * BITS_PER_GBPS),
	           avgPktsize,
	           enqDelta.rxRingDrops,
	           enqDelta.tscEnqLcoreBusy,
	           enqDelta.tscEnqLcoreIdle,
		   (float)(enqDelta.tscEnqLcoreBusy * 100)/(float)(enqDelta.tscEnqLcoreBusy + enqDelta.tscEnqLcoreIdle)
	       );

	// int capacity = rte_ring_get_capacity(ssp->gbsQueue[schedId][0].rxRing);	// Should equal APP_RING_SIZE
	// printf("\nRxRing size=%d, %d entries have cnts: ", capacity, TM_NUM_RX_RINGS);
	// for (int i=0; i<TM_NUM_RX_RINGS; i++)
	// 	printf("%4d ", rte_ring_count(ssp->gbsQueue[schedId][i].rxRing));

	printf("\n====================================================\n");
	secsPrev = secs;
}
