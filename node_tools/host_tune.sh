#!/bin/bash
#
#              © 2025 Nokia
#              Licensed under the BSD 3-Clause Clear License
#              SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Linux host tuning from https://fasterdata.es.net/host-tuning/linux/
cat >> /etc/sysctl.conf <<EOL
# allow testing with buffers up to 128MB
net.core.rmem_max = 536870912 
net.core.wmem_max = 536870912 

net.core.rmem_default = 536870912
net.core.wmem_default = 536870912

# increase Linux autotuning TCP buffer limit to 64MB
net.ipv4.tcp_rmem = 4096 1048576 536870912
net.ipv4.tcp_wmem = 4096 1048576 536870912

# recommended default congestion control is htcp  or bbr
net.ipv4.tcp_congestion_control=bbr
# recommended for hosts with jumbo frames enabled
net.ipv4.tcp_mtu_probing=1
# recommended to enable 'fair queueing'
net.core.default_qdisc = fq
#net.core.default_qdisc = fq_codel
EOL

sysctl --system

# Configure MTU size
for dev in `basename -a /sys/class/net/*`; do
    #ip link set dev $dev mtu 9000
    ip link set dev $dev mtu 1500
done
