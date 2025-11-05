/* tmStats.h
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef TM_STATS_H_
#define TM_STATS_H_

#include "tmDefs.h"

const char* SchedModeEnumToStr(unsigned schedMode);
void SummaryEnqueueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops);
void SummaryDequeueStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs, uint64_t *drops);
void SummaryTxStatsPrint(unsigned schedId, SchedState *ssp, uint32_t secs);
void SummaryEtherPortStatsPrint(unsigned portId, uint32_t secs, uint64_t *drops);

#endif // TM_STATS_H_

