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
tc qdisc del dev $INTERFACE root

# Step 2: specify the new queuing discipline
if [[ ${QDISC_TYPE} == fq ]]; then
    echo ${QDISC_TYPE}
    tc qdisc add dev $INTERFACE root handle 1: fq ce_threshold 10ms limit 1000000 flow_limit 100000
elif [[ ${QDISC_TYPE} == dualpi2 ]]; then
    echo ${QDISC_TYPE}
    tc qdisc add dev $INTERFACE root handle 1: dualpi2 target 10ms
elif [[ ${QDISC_TYPE} == pfifo ]]; then
    echo ${QDISC_TYPE}
    tc qdisc add dev $INTERFACE root handle 1: pfifo limit 1000000
elif [[ ${QDISC_TYPE} == sfq ]]; then
    echo ${QDISC_TYPE}
    tc qdisc add dev $INTERFACE root handle 1: sfq limit 1000000 flows 1023 ecn
else
    tc qdisc add dev $INTERFACE root handle 1: $QDISC_TYPE
fi

# Print the current traffic control settings
tc qdisc show dev $INTERFACE