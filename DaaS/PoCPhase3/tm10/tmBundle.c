/* tmBundle.c
**
**              © 2022-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmBundle.h"
 
#define INT32_CEILING (1E9)
#define INT64_CEILING (1E18)

uint16_t lastQueueServed[NUM_GBSQUEUES_MAX];          // index, not id, of the last queue served for each bundle

void initBundles(void)
{
  for (int i = 0; i < NUM_GBSQUEUES_MAX; i++)
  {
    lastQueueServed[i] = NO_QUEUE;
  }
}

bool bundleQueuesAreEmpty(SchedState *ss, BundleConf *bc)
{
  bool isEmpty = true;
  for (int i = 0; i < bc->numQueues; i++)
  {
    uint16_t qid  = bc->queues[i];
    //#if 0
    if ( !rte_ring_empty(ss->gbsQueue[0][qid].rxRing) )
    {
      isEmpty = false;
      break;
    }
    //#endif
  }
  return isEmpty;
}

uint16_t getNextQueueToServed(BundleConf *bc)
{
  uint16_t qid  = NO_QUEUE;
  uint16_t qidx;

  // if bundle has never been served before, start at the beginning
  if (lastQueueServed[bc->bid] == NO_QUEUE)                          
  {
    qidx = 0;
  }
  // otherwise increment idx
  else
  {
    qidx = lastQueueServed[bc->bid] + 1;
  }

  // if at the end, move to the beginning
  if (qidx >= bc->numQueues)
  {
    qidx = 0;
  }
  
  lastQueueServed[bc->bid] = qidx;
  qid  = bc->queues[qidx];
  
  return qid;
}

void increasePathCredit(SchedConf *sc, PathState *ps, int32_t numTimeslots, uint64_t rtscCurr)
{
  /// Update credit counter for the current bundle
  int64_t creditths = (int64_t) sc->timeslotTsc * sc->timeslotsPerSeq;  // credit limit (same for paths and bundles)
  int64_t gbsCredit = (int64_t) (rtscCurr - ps->pathCredit.lastRtsc) * numTimeslots;

  // First bring the credit value within an acceptable range
  if (unlikely(gbsCredit >= INT64_CEILING))
    {
      gbsCredit = INT64_CEILING;
    }

  // The following may be needed upon the first function call since installing a new configuration
  if ((unlikely(gbsCredit > (2 * creditths))) || (unlikely(gbsCredit < 0)))
    {
      ps->pathCredit.value = creditths;
    }
  else
    {
      ps->pathCredit.value += gbsCredit;

      // See if the available credits have exceeded the maximum allowed
      if (unlikely(ps->pathCredit.value > creditths))
	{
	  ps->pathCredit.value = creditths;
	}
    }

  // Set the time of latest update
  ps->pathCredit.lastRtsc = rtscCurr;
}

void decreasePathCredit(SchedConf *sc, PathState *ps, uint64_t txtimeTsc)
{
  ps->pathCredit.value -= (sc->timeslotsPerSeq * txtimeTsc);
}

void increaseBundleCredit(SchedConf *sc, BundleState *bs, int32_t numTimeslots, uint64_t rtscCurr)
{
  // Update credit counter for the current bundle
  int64_t creditths = (int64_t) sc->timeslotTsc * sc->timeslotsPerSeq;                  // credit limit
  int64_t gbsCredit = (int64_t) (rtscCurr - bs->bundleCredit.lastRtsc) * numTimeslots;

  // First bring the credit value within an acceptable range
  if (unlikely(gbsCredit >= INT64_CEILING))
    {
      gbsCredit = INT64_CEILING;
    }

  // The following may be needed upon the first function call since installing a new configuration
  if ((unlikely(gbsCredit > (2 * creditths))) || (unlikely(gbsCredit < 0)))
    {
      bs->bundleCredit.value = creditths;
    }
  else
    {
      bs->bundleCredit.value += gbsCredit;

      // See if the available credits have exceeded the maximum allowed
      if (unlikely(bs->bundleCredit.value > creditths))
	{
	  bs->bundleCredit.value = creditths;
	}
    }

  // Set the time of latest update
  bs->bundleCredit.lastRtsc = rtscCurr;
}


void decreaseBundleCredit(SchedConf *sc, BundleState *bs, uint64_t txtimeTsc)
{
  bs->bundleCredit.value -= (sc->timeslotsPerSeq * txtimeTsc);
}

void increaseQueueCredit(SchedConf *sc, QueueState *qs, int32_t numTimeslots, uint64_t rtscCurr)
{
  // Update credit counter for the current queue
  int64_t creditths = (int64_t) sc->timeslotTsc * sc->timeslotsPerSeq;                  // credit limit
  int64_t gbsCredit = (int64_t) (rtscCurr - qs->queueCredit.lastRtsc) * numTimeslots;

  // First bring the credit increment value within an acceptable range
  if (unlikely(gbsCredit >= INT64_CEILING))
  {
    gbsCredit = INT64_CEILING;
  }

  // The following may be needed upon the first function call since installing a new configuration
  if ((unlikely(gbsCredit > (2 * creditths))) || (unlikely(gbsCredit < 0)))
    {
      qs->queueCredit.value = creditths;
    }
  else
    {
      qs->queueCredit.value += gbsCredit;

      // See if the available credits have exceeded the maximum allowed
      if (unlikely(qs->queueCredit.value > creditths))
	{
	  //DBGLOG("credit saturated for slot#%u,queue=%u at tsc %18"PRIu64"\n", ss->timeslotIdx, gbsQIdx, rtscCurr);
	  qs->queueCredit.value = creditths;
	}
    }

  // Set the time of latest update
  qs->queueCredit.lastRtsc = rtscCurr;
}

void decreaseQueueCredit(SchedConf *sc, QueueState *qs)
{
  qs->queueCredit.value -= (sc->timeslotsPerSeq * sc->timeslotTsc);
}



