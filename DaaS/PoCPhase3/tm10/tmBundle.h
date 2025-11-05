/* tmBundle.h
*
**              © 2022-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef TM_BUNDLE_H_
#define TM_BUNDLE_H_

#include <inttypes.h>

#include "tmDefs.h"

#define NO_QUEUE        0xFFFF

void initBundles(void);

bool bundleQueuesAreEmpty(SchedState *ss, BundleConf *bc);                   // All queues in bundle are empty

uint16_t getNextQueueToServed(BundleConf *bc);                               // Get the next queue (in RR) that should be served

void increasePathCredit(SchedConf *sc, PathState *ps, int32_t numTimeslots, uint64_t rtscCurr);

void decreasePathCredit(SchedConf *sc, PathState *ps, uint64_t rtscCurr);

void increaseBundleCredit(SchedConf *sc, BundleState *bs, int32_t numTimeslots, uint64_t rtscCurr);

void decreaseBundleCredit(SchedConf *sc, BundleState *bs, uint64_t rtscCurr);

void increaseQueueCredit(SchedConf *sc, QueueState *qs, int32_t numTimeslots, uint64_t txtimeTsc);

void decreaseQueueCredit(SchedConf *sc, QueueState *qs);

#endif // TM_BUNDLE_H_
