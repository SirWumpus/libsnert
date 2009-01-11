#!/bin/sh
#
# Collect system statistics.
#
# Copyright 2008 by Anthony Howe.  All rights reserved.
#
# crontab -e
#
# 1-59/5  *  *  *  *  /usr/local/sbin/collect.sh
#
# /etc/newsyslog.conf:
#
# /var/log/stats/fs.log     640  2     *    @T2358  Z
# /var/log/stats/la.log     640  2     *    @T2358  Z
# /var/log/stats/net.log    640  2     *    @T2358  Z
# /var/log/stats/pftop.log  640  2     *    @T2358  Z
# /var/log/stats/ps.log     640  2     *    @T2358  Z
# /var/log/stats/top.log    640  2     *    @T2358  Z
# /var/log/stats/vm.log     640  2     *    @T2358  Z


STATS_DIR=/var/log/stats

FS_LOG=${STATS_DIR}/fs.log
LA_LOG=${STATS_DIR}/la.log
NET_LOG=${STATS_DIR}/net.log
PS_LOG=${STATS_DIR}/ps.log
PFTOP_LOG=${STATS_DIR}/pftop.log
TOP_LOG=${STATS_DIR}/top.log
VM_LOG=${STATS_DIR}/vm.log

DMESG_SAVE=${STATS_DIR}/dmesg.copy
DMESG_SIZE=${STATS_DIR}/dmesg.size

NOW=`date +'%Y-%m-%d %H:%M:%S'`

if test ! -d ${STATS_DIR} ; then
	mkdir -p ${STATS_DIR}
	chmod 755 ${STATS_DIR}
fi

# Remember size of dmesg in case alerts start to appear.
ls -s /var/run/dmesg.boot >${DMESG_SIZE}.$$
if ! cmp ${DMESG_SIZE} ${DMESG_SIZE}.$$ ; then
        mv ${DMESG_SAVE} ${DMESG_SAVE}.previous
	dmesg > ${DMESG_SAVE}
fi
mv ${DMESG_SIZE}.$$ ${DMESG_SIZE}

# Uptime snapshot
uptime | sed -e "s/^.*load averages: \(.*\), \(.*\), \(.*\)/${NOW} \1 \2 \3/" >>${LA_LOG}

# Process list snap shot
echo "-- ${NOW} --" >>${PS_LOG}
ps awxl >>${PS_LOG}

# Process list snap shot
echo "-- ${NOW} --" >>${TOP_LOG}
top -b >>${TOP_LOG}

# Process list snap shot
echo "-- ${NOW} --" >>${PFTOP_LOG}
pftop -b >>${PFTOP_LOG}

# Network connection snap shot
echo "-- ${NOW} --" >>${NET_LOG}
netstat -na -f inet >>${NET_LOG}

# Kernel activity snap shot
echo "-- ${NOW} --" >>${VM_LOG}
vmstat >>${VM_LOG}

# File status snap shot
echo "-- ${NOW} --" >>${FS_LOG}
fstat >>${FS_LOG}

exit 0

