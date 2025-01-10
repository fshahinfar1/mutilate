#!/bin/bash
set -e

SERVER_HOST=192.168.1.1
SERVER_PORT=11211
SERVER_UDP_PORT=8080
# NOTE: Experiment duration in seconds
TIME=30
LOG_FILE=/tmp/bmc_performance.txt

LOCALHOST=`hostname`
AGENT=$LOCALHOST

# WORKLOAD_DESC="--records=1000000 --keysize=fb_key --valuesize=fb_value --iadist=fb_ia --update=0"
WORKLOAD_DESC="--records=10000 -K 8 -V 8"

trap "handle_signal" SIGINT SIGHUP


function handle_signal {
	pkill mutilateudp
}

echo Loading ...
./mutilate -s $SERVER_HOST:$SERVER_PORT $WORKLOAD_DESC --loadonly -t 1
sleep 1

for i in $(seq 1); do
	echo Running agents ...
	./mutilateudp -A --threads 1 &
	echo Running experiment $i ...
	./mutilateudp --time=$TIME $WORKLOAD_DESC \
		--server=$SERVER_HOST:$SERVER_UDP_PORT \
		--qps=2000 \
		--noload --threads=1 --connections=1 \
		--measure_connections=1 --measure_qps=2000 \
		--agent=$AGENT &>> $LOG_FILE
	# Terminate
	handle_signal
	sleep 1
done

echo '=============================='
cat $LOG_FILE
