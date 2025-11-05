/* tmFlow.h
*
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#ifndef FLOW_DEF_H_
#define FLOW_DEF_H_

struct rte_flow* TmRxFlowConfig(uint16_t port_id, uint16_t rxQueueId, uint16_t vlanTci, uint16_t vlanTciMask);

#endif // FLOW_DEF_H_
