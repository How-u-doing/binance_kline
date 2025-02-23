#include "../shm_bbuffer_spmc.h"
#include "data.h"

#include <fstream>
#include <queue>
#include <vector>
#include <thread>
#include <unordered_map>

class CumMedian {
public:
    CumMedian() = default;

    void insert(int32_t x) {
        if (low.empty() || x <= low.top())
            low.push(x);
        else
            high.push(x);

        if (low.size() > high.size() + 1) {
            high.push(low.top());
            low.pop();
        } else if (high.size() > low.size()) {
            low.push(high.top());
            high.pop();
        }
    }

    int32_t get_median() const {
        if (low.size() == high.size())
            return (low.top() + high.top()) / 2.0;
        return low.top();
    }

private:
    std::priority_queue<int32_t> low;
    std::priority_queue<int32_t, std::vector<int32_t>, std::greater<int32_t>> high;
};

#ifndef MEDIAN_FACTOR
#define MEDIAN_FACTOR 1
#endif

struct StatData {
    uint64_t vol = 0;
    uint64_t num_trades = 0;
#if MEDIAN_FACTOR
    CumMedian cum_median;
#endif
    int32_t factor = 0;
};

typedef std::unordered_map<uint32_t, StatData> StatMap;

void update_factor(StatMap &stat, const KLineData &kline) {
    auto it = stat.find(kline.sym_id);
    if (it == stat.end())
        it = stat.emplace(kline.sym_id, StatData{}).first;

    StatData &data = it->second;
    data.vol += kline.volume;
    data.num_trades += kline.num_trades;

    int32_t typical_price = (kline.high + kline.low + kline.close) / 3;
#if MEDIAN_FACTOR
    data.cum_median.insert(kline.close);
    int32_t median = data.cum_median.get_median();
    data.factor += typical_price < median ? 1 : -1;
#else
    data.factor += typical_price < kline.close ? 1 : -1;
#endif
}

template <typename T>
// using ShmConsumer = shm_spmc::PShmBBufferLockFree<T, /* IsProducer = */ false>;
using ShmConsumer = shm_spmc::PShmBBufferGiacomoni<T, /* IsProducer = */ false>;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <shm_name> <out_file> \n", argv[0]);
        return -1;
    }

    const char *shm_name = argv[1];
    const char *out_file = argv[2];

    ShmConsumer<KLineData> shm_buffer(shm_name);
    StatMap stat;
    KLineData kline;

    constexpr int delta_print_time = 10'00'000;  // every 10 min
    int print_time = 9'30'00'000;
    while (true) {
        int rc = shm_buffer.consume(kline);
        if (rc == CONSUME_FINISHED)
            break;

        if (rc == CONSUME_SUCCESS) {
            if (kline.time >= print_time) {
                printf("consumer current timepoint: %d\n", kline.time);
                fflush(stdout);
                print_time = kline.time + delta_print_time;
            }
            update_factor(stat, kline);
        } else {  // CONSUME_AGAIN
            printf("waiting for new data to consume, sleeping for 1ms\n");
            fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    std::ofstream ofs(out_file);
    ofs << "sym_id,vol,num_trades,factor\n";

    for (const auto &[sym_id, data] : stat)
        ofs << sym_id << "," << data.vol << "," << data.num_trades << "," << data.factor << "\n";
}
