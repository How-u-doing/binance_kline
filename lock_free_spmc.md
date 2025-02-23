## Lock-free SPMC Queue

```bash
$ make producer consumer
$ ./launch_spmc.sh 
Usage: ./launch_spmc.sh <shm_name> <size_gb> <sym_cnt> <num_consumers>

$ ./launch_spmc.sh /myshm 3 7000 0  # just run the producer
$ ./launch_spmc.sh /myshm 3 7000 1  # run the producer and 1 consumer simultaneously
$ ./launch_spmc.sh /myshm 3 7000 5  # run the producer and 5 consumers simultaneously

$ # observe the time elapsed by the producer on another terminal
$ tail -f logs/producer.log
...

$ # observe the time elapsed by consumer #1 on another terminal
$ tail -f logs/consumer_1.log
...
```
