#!/bin/bash
logging_file=$MPI_LOGGING_DIR/rank_$OMPI_COMM_WORLD_RANK.log
mkdir $MPI_LOGGING_DIR
source /root/ip.sh
$@ 2>&1 | tee $logging_file
