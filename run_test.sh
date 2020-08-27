#!/bin/bash
if [ "$#" -lt 4 ]; then echo "$(tput setaf 1)[ERROR]$(tput sgr 0) test name, number of nodes, input size & n_trials required"; exit -1; fi
if [ "$#" -gt 7 ]; then echo "$(tput setaf 1)[ERROR]$(tput sgr 0) too many arguments: $#"; exit -1; fi

trap "trap - SIGTERM && kill -- -$$" SIGINT SIGTERM SIGHUP EXIT

sudo fuser -k 6666/tcp -s &> /dev/null
sudo fuser -k 50055/tcp -s &> /dev/null

## setup
ROOT_DIR=$(dirname $(realpath -s $0))

test_name=$1
test_executable=$test_name
test_executable_abspath=$ROOT_DIR/$test_executable
world_size=$2
object_size=$3
n_trials=$4

if [ ! -f $test_executable_abspath ]; then
    echo "$(tput setaf 1)[ERROR]$(tput sgr 0) test executable not found: $test_executable_abspath"
    exit -2
fi

if [ "$#" -eq 4 ]; then
    # get cluster info
    source $ROOT_DIR/load_cluster_env.sh
    OTHERS_IPADDR=(${OTHERS_IPADDR[@]:0:$(($world_size-1))})
    echo "$(tput setaf 2)[INFO]$(tput sgr 0) master: $MY_IPADDR; others: ${OTHERS_IPADDR[@]}"

    # prompt test info
    echo "$(tput setaf 2)[INFO]$(tput sgr 0) Running test $(tput setaf 3)$(tput bold)$test_name$(tput sgr 0)"

    # create logging dir
    log_dir=$ROOT_DIR/log/$(date +"%Y%m%d-%H%M%S")-$test_name-$world_size-$object_size
    mkdir -p $log_dir
    ln -sfn $log_dir/ $ROOT_DIR/log/latest

    pkill notification
    sleep 0.5
    (./notification $MY_IPADDR 2>&1 | tee $log_dir/$MY_IPADDR.notification.log) &
    sleep 0.5

    # execute remote commands
    for index in ${!OTHERS_IPADDR[@]}
    do
        rank=$((index+1))
        ssh -t -t ${OTHERS_IPADDR[$index]} "$(realpath -s $0) $test_name $world_size $object_size $n_trials $MY_IPADDR $rank $log_dir" &
    done
    # start local process (rank=0)
    $test_executable_abspath $MY_IPADDR $MY_IPADDR $world_size 0 $object_size $n_trials 2>&1 | tee $log_dir/$MY_IPADDR.server.log
    sleep 1
else
    my_address=$(hostname -i)

    redis_address=$5
    rank=$6
    log_dir=$7
    echo "$(tput setaf 2)[INFO]$(tput sgr 0) Node($my_address) redis_address: $redis_address world_size: $world_size rank: $5 object_size: $object_size"
    $test_executable_abspath $redis_address $my_address $world_size $rank $object_size $n_trials 2>&1 | tee $log_dir/$my_address.client.log
    sleep 1
fi
