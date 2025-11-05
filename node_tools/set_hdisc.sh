#!/bin/bash
#
#              © 2025 Nokia
#              Licensed under the BSD 3-Clause Clear License
#              SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Usage: set_qdisc.sh <interfacename> <linkcapacity> <qdisctype> 
INTERFACE=$1
CAPACITY=$2
QDISC_TYPE=$3
#
# Constant values
MAX_LATENCY=100     # [ms]
# 
# Step 1: remove the existing configuration
sudo tc qdisc del dev $INTERFACE root

# Step 2: set the rate limit for the interface
sudo tc qdisc add dev $INTERFACE root handle 1: tbf rate ${CAPACITY}Mbit latency ${MAX_LATENCY}ms burst 500kb

# Step 3: add a set of per-class strict-priority queues
sudo tc qdisc add dev $INTERFACE parent 1:1 handle 10: prio bands 3 priomap 0 2 2 2 1 2 0 0 1 1 1 1 1 1 1 1 

# Step 4: specify the queuing discipline for each prio class
if [[ ${QDISC_TYPE} == fq ]]; then
    echo ${QDISC_TYPE}
    sudo tc qdisc add dev $INTERFACE parent 10:1 handle 100: fq ce_threshold 10ms limit 1000000 flow_limit 100000
    sudo tc qdisc add dev $INTERFACE parent 10:2 handle 200: fq ce_threshold 10ms limit 1000000 flow_limit 100000
    sudo tc qdisc add dev $INTERFACE parent 10:3 handle 300: fq ce_threshold 10ms limit 1000000 flow_limit 100000
elif [[ ${QDISC_TYPE} == dualpi2 ]]; then
    echo ${QDISC_TYPE}
    sudo tc qdisc add dev $INTERFACE parent 10:1 handle 100: dualpi2 target 10ms
    sudo tc qdisc add dev $INTERFACE parent 10:2 handle 200: dualpi2 target 10ms
    sudo tc qdisc add dev $INTERFACE parent 10:3 handle 300: dualpi2 target 10ms
elif [[ ${QDISC_TYPE} == pfifo ]]; then
    echo ${QDISC_TYPE}
    sudo tc qdisc add dev $INTERFACE parent 10:1 handle 100: pfifo limit 1000000
    sudo tc qdisc add dev $INTERFACE parent 10:2 handle 200: pfifo limit 1000000
    sudo tc qdisc add dev $INTERFACE parent 10:3 handle 300: pfifo limit 1000000
elif [[ ${QDISC_TYPE} == sfq ]]; then
    echo ${QDISC_TYPE}
    sudo tc qdisc add dev $INTERFACE parent 10:1 handle 100: sfq limit 1000000 flows 1023 ecn
    sudo tc qdisc add dev $INTERFACE parent 10:2 handle 200: sfq limit 1000000 flows 1023 ecn
    sudo tc qdisc add dev $INTERFACE parent 10:3 handle 300: sfq limit 1000000 flows 1023 ecn
else
    sudo tc qdisc add dev $INTERFACE parent 10:1 handle 100: $QDISC_TYPE
    sudo tc qdisc add dev $INTERFACE parent 10:2 handle 200: $QDISC_TYPE
    sudo tc qdisc add dev $INTERFACE parent 10:3 handle 300: $QDISC_TYPE
fi

# Print the current traffic control settings
sudo tc qdisc show dev $INTERFACE
