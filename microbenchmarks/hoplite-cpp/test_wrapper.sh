#!/bin/bash
source /root/ip.sh
mkdir -p $HOPLITE_LOGGING_DIR
logging_file=$HOPLITE_LOGGING_DIR/rank_$OMPI_COMM_WORLD_RANK.log
$@ 2>&1 | tee $logging_file
