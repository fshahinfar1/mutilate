#!/bin/bash
# This script runs the DPDK version of mutilate

set -e

if [ ! -f ./mutilatedpdk ]; then
	echo Binary not found, probably not built the DPDK version.
	exit 1
fi


# NOTE: add the key-value distributions that you need to these commands

# First you need to load the keys into the server with the following command
# using the normal mutilate
#     ./mutilate --server 192.168.1.1:11211 --binary --loadonly

./mutilatedpdk \
	--server "192.168.1.1:11211" \
	--binary \
	-c 1 -C 1 \
	--my-mac="9c:dc:71:5d:d5:c1" \
	--my-ip=192.168.1.2 \
	--server-mac="9c:dc:71:49:b8:d1" \
	--cpu-core="2" \
	--noload
