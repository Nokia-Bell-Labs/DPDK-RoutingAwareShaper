#! /bin/sh
#
# Script for launching the traffic manager for the forward path.
#
#              © 2025 Nokia
#              Licensed under the BSD 3-Clause Clear License
#              SPDX-License-Identifier: BSD-3-Clause-Clear
#
SPEED=$1
CFG_FILE=$2
INTERVAL=$3
#
# In the following line, 'ubuntu' must be replaced with the actual username if different
export DPDK="/home/ubuntu/DPDK/v22.11.10/dpdk-stable-22.11.10"
export TM10="${DPDK}/DaaS/PoCPhase3/tm10"
# Configuration for single-cable setup
#${DPDK}/build/DaaS/PoCPhase3/dpdk-tm10 -l 12,13,14,15 -a 09:00.0 -a 09:00.0 -n 3 --file-prefix rte3 -- --speed "$SPEED" --pfc "0,0,0,13,14,15,${TM10}/cfg/tm10/intf.cfg,${TM10}/cfg/tm10/$CFG_FILE" --stp "$INTERVAL"
# Configuration for double-cable setup
${DPDK}/build/DaaS/PoCPhase3/dpdk-tm10 -l 12,13,14,15 -a 09:00.0 -a 07:00.0 -n 3 --file-prefix rte3 -- --speed "$SPEED" --pfc "0,1,0,13,14,15,${TM10}/cfg/tm10/intf.cfg,${TM10}/cfg/tm10/$CFG_FILE" --stp "$INTERVAL"
