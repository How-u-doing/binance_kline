## Single-Producer Multiple-Consumer Bounded Buffer
```bash
$ make check_huge_pages_info
cat /proc/meminfo | grep Huge
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
FileHugePages:         0 kB
HugePages_Total:       0
HugePages_Free:        0
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:               0 kB
$ make enable_huge_pages
echo 128 | sudo tee /proc/sys/vm/nr_hugepages
128
echo `id -g mark` | sudo tee /proc/sys/vm/hugetlb_shm_group
1001
$ make all
$ ./shm_bbuffer_spmc_kline
Usage:
./shm_bbuffer_spmc_kline producer shm_key capacity
./shm_bbuffer_spmc_kline consumer shm_id capacity
$ ./shm_bbuffer_spmc_kline producer 1234 10
Message received: {"e":"kline","E":1734255588017,"s":"BTCUSDT","k":{"t":1734255540000,"T":1734255599999,"s":"BTCUSDT","i":"1m","f":4273836662,"L":4273837179,"o":"102070.35000000","c":"102059.72000000","h":"102070.35000000","l":"102059.71000000","v":"5.23413000","n":518,"x":false,"q":"534233.83694960","V":"0.41164000","Q":"42012.14866260","B":"0"}}
Event time: UTC: 2024-12-15 09:39:48.017
Symbol: BTCUSDT
Kline data:
  Start time: UTC: 2024-12-15 09:39:00.000
  Open: 102070.35000000
  High: 102070.35000000
  Low: 102059.71000000
  Close: 102059.72000000
  Volume: 5.23413000
  Is kline closed: 0

...
$ # on another terminal
$ ipcs -m
------ Shared Memory Segments --------
key        shmid      owner      perms      bytes      nattch     status
0x004742cc 0          postgres   600        56         6
0x000004d2 15         mark       600        4112       1
$ ./shm_bbuffer_spmc_kline consumer 15 10  # you can launch multiple consumers
...
$ # on another terminal
$ make check_huge_pages_info
cat /proc/meminfo | grep Huge
AnonHugePages:         0 kB
ShmemHugePages:        0 kB
FileHugePages:         0 kB
HugePages_Total:     128
HugePages_Free:      127
HugePages_Rsvd:        0
HugePages_Surp:        0
Hugepagesize:       2048 kB
Hugetlb:          262144 kB
```

## Huge Pages
![](img/Core_i7_Address_Translation.png)
![](img/Huge_Pages.png)
