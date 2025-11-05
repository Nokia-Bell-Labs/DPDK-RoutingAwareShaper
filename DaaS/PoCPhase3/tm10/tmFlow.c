/*  tmFlow.c
**
**              © 2020-2025 Nokia
**              Licensed under the BSD 3-Clause Clear License
**              SPDX-License-Identifier: BSD-3-Clause-Clear
**
*/

#include "tmDefs.h"
#include "tmFlow.h"
#include "parserLib.h"

/**
 * Create a flow rule that sends packets with matching vlans to selected queue.
 * Flow action RTE_FLOW_ACTION_TYPE_QUEUE maps matching flow rule to a rx queue (rxQueueId).
 *
 * @param port_id
 *   The selected port.
 * @param rxQueueId
 *   maps matching vlans to rx queue
 * @param vlanTci
 *   flow rule for vlanId (ls 12 bits) + priority
 * @param vlanIdMask
 *   The mask to apply to vlanId
 * @return
 *   A flow if the rule could be created else return NULL.
 */
struct rte_flow *
TmRxFlowConfig(uint16_t port_id, uint16_t rxQueueId, uint16_t vlanTci, uint16_t vlanTciMask)
{
#define MAX_PATTERN_NUM		3
#define MAX_ACTION_NUM		2
	struct rte_flow_attr attr;
	struct rte_flow_item pattern[MAX_PATTERN_NUM];
	struct rte_flow_action action[MAX_ACTION_NUM];
	struct rte_flow *flow = NULL;
	struct rte_flow_action_queue queue;
	struct rte_flow_item_eth  eth_spec;
	struct rte_flow_item_eth  eth_mask;
	struct rte_flow_item_vlan vlan_spec;
	struct rte_flow_item_vlan vlan_mask;
	struct rte_flow_error err;
	int res;

	memset(pattern, 0, sizeof(pattern));
	memset(action, 0, sizeof(action));
	memset(&eth_spec, 0, sizeof(struct rte_flow_item_eth));
	memset(&eth_mask, 0, sizeof(struct rte_flow_item_eth));
	memset(&vlan_spec, 0, sizeof(struct rte_flow_item_vlan));
	memset(&vlan_mask, 0, sizeof(struct rte_flow_item_vlan));

	/*
	 * set the rule attribute.
	 * in this case only ingress packets will be checked.
	 */
	memset(&attr, 0, sizeof(struct rte_flow_attr));
	attr.ingress = 1;

	/*
	 * create the action sequence.
	 * one action only,  move packet to queue
	 */
	queue.index    = rxQueueId;
	action[0].type = RTE_FLOW_ACTION_TYPE_QUEUE;
	action[0].conf = &queue;
	action[1].type = RTE_FLOW_ACTION_TYPE_END;

	/*
	 * set the first level of the pattern (ETH) to parse all packets.
	 */
	pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;
	pattern[0].spec = &eth_spec;
	pattern[0].mask = &eth_mask;

	/*
	 * setting the second level of the pattern (IP).
	 * in this example this is the level we care about
	 * so we set it according to the parameters.
	 */
	vlan_spec.tci = RTE_BE16(vlanTci);
	vlan_mask.tci = RTE_BE16(vlanTciMask);
	pattern[1].type = RTE_FLOW_ITEM_TYPE_VLAN;
	pattern[1].spec = &vlan_spec;
	pattern[1].mask = &vlan_mask;

	/* the final level must be always type end */
	pattern[2].type = RTE_FLOW_ITEM_TYPE_END;

	res = rte_flow_validate(port_id, &attr, pattern, action, &err);
	if (!res)
		flow = rte_flow_create(port_id, &attr, pattern, action, &err);

	return flow;
}
