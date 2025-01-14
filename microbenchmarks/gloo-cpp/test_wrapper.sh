#!/bin/bash
logging_file=$GLOO_LOGGING_DIR/rank_$OMPI_COMM_WORLD_RANK.log
mkdir -p $GLOO_LOGGING_DIR
source /root/ip.sh
$GLOO_DIR/build/gloo/benchmark/benchmark \
    --size $OMPI_COMM_WORLD_SIZE \
    --rank $OMPI_COMM_WORLD_RANK \
    --redis-host $REDIS_HOST \
    --redis-port 7799 \
    --prefix benchmark-$test_name-$OMPI_COMM_WORLD_SIZE-$object_size \
    --transport tcp \
    --tcp-device enp0s4 \
    --elements $(($object_size / 4)) \
    --iteration-count 1 \
    $test_name \
    2>&1 | tee $logging_file
