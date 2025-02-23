#! /bin/bash

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <shm_name> <size_gb> <sym_cnt> <num_consumers>"
    exit 1
fi

shm_name=$1
size_gb=$2
sym_cnt=$3
num_consumers=$4

mkdir -p logs

(time ./producer $shm_name $size_gb $sym_cnt) > logs/producer.log 2>&1 &

for i in $(seq 1 $num_consumers); do
    (time ./consumer $shm_name res_$i.csv) > logs/consumer_$i.log 2>&1 &
done

wait
