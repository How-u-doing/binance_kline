#include "../shm_bbuffer_spmc.h"
#include "data.h"

#include <random>
#include <cstring>
#include <cstdio>
#include <cstdlib>

template <typename T>
// using ShmProducer = shm_spmc::PShmBBufferLockFree<T, /* IsProducer = */ true>;
using ShmProducer = shm_spmc::PShmBBufferGiacomoni<T, /* IsProducer = */ true>;

std::random_device rd;
std::mt19937 gen(rd());
std::uniform_int_distribution<int> dis(0, 20);

void fill_data(KLineData &data, int k, int t) {
    int rand = dis(gen);

    data.sym_id = k;
    data.time = t;
    data.volume = k + rand;
    data.num_trades = rand;
    data.open = k + (rand & 5);
    data.high = k + (rand & 13);
    data.low = k - (rand & 7);
    data.close = k + (rand & 3);
}

void produce_data(ShmProducer<KLineData> &shm_buffer, int sym_cnt) {
    gen.seed(12345);  // set seed for reproducibility
    KLineData data;

    constexpr int delta_print_time = 10'00'000;  // every 10 min
    int print_time = 9'30'00'000;
    int t = 9'30'00'000;
    int delta_t = 3'000;

    while (t <= 16'00'00'000) {
        if (t / 1'00'000 % 100 >= 60) {
            t += 40'00'000;
            print_time += 40'00'000;
            continue;
        }
        if (t >= print_time) {
            printf("producer current timepoint: %d\n", t);
            fflush(stdout);
            print_time += delta_print_time;
        }

        for (int k = 1; k <= sym_cnt; k++) {
            fill_data(data, k, t);
            if (!shm_buffer.produce(data)) {
                printf("Failed to produce data: max size reached!\n");
                fflush(stdout);
                return;
            }
        }

        t += delta_t;
    }
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <shm_name> <size_gb> <sym_cnt>\n", argv[0]);
        return -1;
    }

    const char *shm_name = argv[1];
    double size_gb = std::atof(argv[2]);
    const int sym_cnt = std::atoi(argv[3]);
    printf("shm_name: %s\nsym_cnt: %d\n", shm_name, sym_cnt);

    constexpr size_t GB = 1024 * 1024 * 1024;
    const size_t max_cap = size_gb * GB / sizeof(KLineData);
    ShmProducer<KLineData> shm_buffer(shm_name, max_cap);

    produce_data(shm_buffer, sym_cnt);

    return 0;
}
