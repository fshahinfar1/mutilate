#!/bin/bash
set -e

SERVER_HOST=192.168.200.101
SERVER_PORT=11211
# NOTE: Experiment duration in seconds
TIME=60

LOCALHOST=`hostname`
# NOTE: if REMOTEHOST is not set then it is not configured and REMOTECMD is not
# executed
REMOTEHOST=
# NOTE: RUNLOCAL can be `master` or `agent`. It decides if master or agent node
# should run locally
RUNLOCAL=master
AGENT_CMDLINE='./mutilateudp -A --threads 4'
MASTER_CMDLINE='./mutilateudp --records=1000000 --time=$TIME --qps=10000
--keysize=fb_key --valuesize=fb_value --iadist=fb_ia --update=0
--server=$SERVER_HOST:$SERVER_PORT --noload --threads=1
--connections=4 --measure_connections=32
--measure_qps=2000 --binary'
# --agent=$AGENT

if [ $RUNLOCAL == agent ]; then
	AGENT=$LOCALHOST
	LOCALCMD=$AGENT_CMDLINE
	REMOTECMD=`eval echo $MASTER_CMDLINE`
elif [ $RUNLOCAL == master ]; then
	AGENT=$REMOTEHOST
	LOCALCMD=`eval echo $MASTER_CMDLINE`
	REMOTECMD=$AGENT_CMDLINE
fi
REMOTEBIN=${REMOTECMD/%\ */}
REMOTEBIN=${REMOTEBIN:2}

echo -e "\033[32m --- Run local: $LOCALCMD\033[0m"
if [ "x$REMOTEHOST" != "x" ]; then
	echo -e "\033[32m --- Run remote: $REMOTECMD\033[0m"
else
	echo -e "\033[32m --- Run remote: not configured\033[0m"
fi


# Compile mutilate if it is not compiled yet
scons

if [ "x$REMOTEHOST" != "x" ]; then
	# Configure and run the remote node
	trap 'ssh $REMOTEHOST pkill $REMOTEBIN' EXIT
	scp -q $REMOTEBIN $REMOTEHOST:
	ssh $REMOTEHOST "ulimit -c unlimited; stdbuf -oL $REMOTECMD" &
fi

if [ x$1 == x -o x$1 == xrun ]; then
	$LOCALCMD
elif [ $1 == strace ]; then
	strace -e '!epoll_wait' -f $LOCALCMD
elif [ $1 == gdb ]; then
	gdb --args $LOCALCMD
fi
