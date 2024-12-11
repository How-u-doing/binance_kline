#include "shm_bbuffer_spmc.h"

#include <iostream>
#include <csignal>

sig_atomic_t stop_producer = 0;

void producer_sigint_handler(int signal) {
    if (signal == SIGINT) {
        stop_producer = 1;
    }
}

void run_producer(const char *shm_name, idx_t capacity) {
    ShmCircularBuffer<int, /* IsProducer: */ true> shm_bbuffer(shm_name, capacity);
    signal(SIGINT, producer_sigint_handler);

    int item = -1;
    while (!stop_producer && std::cin >> item) {
        shm_bbuffer.produce(item);
        std::cout << "Produced: " << item << "\n";
    }
}

void run_consumer(const char *shm_name) {
    ShmCircularBuffer<int, /* IsProducer: */ false> shm_bbuffer(shm_name);

    int item = -1;
    while (true) {
        shm_bbuffer.consume(&item);
        std::cout << "Consumed: " << item << "\n";
    }
}

void print_usage_and_exit(const char *app) {
    std::cerr << "Usage: \n"
              << app << " producer /shm_name capacity\n"
              << app << " consumer /shm_name\n";
    exit(EXIT_FAILURE);
}

int main(int argc, const char *argv[]) {
    const char *app = argv[0];
    if (argc == 1)
        print_usage_and_exit(app);
    const std::string app_kind = argv[1];
    if (app_kind == "producer") {
        if (argc != 4)
            print_usage_and_exit(app);
        idx_t capacity = std::stod(argv[3]);
        if (capacity <= 0) {
            std::cerr << "invalid capacity\n";
            exit(EXIT_FAILURE);
        }
        const char *shm_name = argv[2];
        run_producer(shm_name, capacity);
    } else if (app_kind == "consumer") {
        if (argc != 3)
            print_usage_and_exit(app);
        const char *shm_name = argv[2];
        run_consumer(shm_name);
    } else {
        print_usage_and_exit(app);
    }

    return 0;
}
