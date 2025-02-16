#include "shm_bbuffer_spmc.h"

#include <iostream>
#include <csignal>

using shm_spmc::idx_t;
using shm_spmc::PShmCircularBuffer;

sig_atomic_t stop_producer = 0;

void producer_sigint_handler(int signal) {
    if (signal == SIGINT) {
        stop_producer = 1;
    }
}

void run_producer(const char *shm_name, idx_t capacity) {
    PShmCircularBuffer<int, /* IsProducer: */ true> shm_bbuffer(shm_name, capacity);
    signal(SIGINT, producer_sigint_handler);

    int item = -1;
    while (!stop_producer && std::cin >> item) {
        shm_bbuffer.produce(item);
        std::cout << "Produced: " << item << "\n";
    }
}

void run_consumer(const char *shm_name, idx_t capacity) {
    PShmCircularBuffer<int, /* IsProducer: */ false> shm_bbuffer(shm_name, capacity);

    int item = -1;
    while (true) {
        shm_bbuffer.consume(&item);
        std::cout << "Consumed: " << item << "\n";
    }
}

void print_usage_and_exit(const char *app) {
    std::cerr << "Usage:\n"
              << app << " producer /shm_name capacity\n"
              << app << " consumer /shm_name capacity\n";
    exit(EXIT_FAILURE);
}

int main(int argc, const char *argv[]) {
    const char *app = argv[0];
    if (argc != 4)
        print_usage_and_exit(app);
    const std::string app_kind = argv[1];
    const char *shm_name = argv[2];
    const idx_t capacity = std::stoul(argv[3]);
    if (capacity <= 0) {
        std::cerr << "invalid capacity\n";
        exit(EXIT_FAILURE);
    }
    if (app_kind == "producer") {
        run_producer(shm_name, capacity);
    } else if (app_kind == "consumer") {
        run_consumer(shm_name, capacity);
    } else {
        print_usage_and_exit(app);
    }

    return 0;
}
