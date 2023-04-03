#!/bin/bash
all_hosts=$1
shift
# This syntax is for OpenMPI
mpirun --allow-run-as-root --mca btl_tcp_if_exclude lo,docker0,enp0s5 --map-by ppr:1:node -H $all_hosts $@
