#pragma once

#include <string>
#include <type_traits>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <unistd.h>

#define handle_error(msg)   \
    do {                    \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

typedef unsigned idx_t;

// Single-producer multiple-consumer bounded buffer using shared memory.
// see also https://en.wikipedia.org/wiki/Producer-consumer_problem
template <typename T, bool IsProducer>
class ShmCircularBuffer {
public:
    explicit ShmCircularBuffer(const char *shm_name, idx_t capacity = 0) : shm_name_(shm_name) {
        static_assert(std::is_trivial_v<T>, "T must be a trivial type");
        if constexpr (IsProducer) {
            shm_fd_ = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
        } else {
            shm_fd_ = shm_open(shm_name, O_RDWR, 0600);
        }
        if (shm_fd_ == -1)
            handle_error("shm_open");

        size_t shm_size = sizeof(ControlBlock) + sizeof(T) * capacity;
        if constexpr (IsProducer) {
            if (ftruncate(shm_fd_, shm_size) == -1)
                handle_error("ftruncate");
        }

        // TODO: use huge pages
        void *shmp = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0);
        if (shmp == MAP_FAILED)
            handle_error("mmap");

        // initialize the control block
        cb_ = static_cast<ControlBlock *>(shmp);
        if constexpr (IsProducer) {
            if (sem_init(&cb_->mutex, /* pshared: */ 1, /* value: */ 1) == -1)
                handle_error("sem_init-mutex");
            if (sem_init(&cb_->full, /* pshared: */ 1, /* value: */ 0) == -1)
                handle_error("sem_init-full");
            if (sem_init(&cb_->empty, /* pshared: */ 1, /* value: */ capacity) == -1)
                handle_error("sem_init-empty");
            cb_->cap = capacity;
            cb_->head = 0;
            cb_->len = 0;
        }
        buffer_ = reinterpret_cast<T *>(static_cast<char *>(shmp) + sizeof(ControlBlock));
    }

    ~ShmCircularBuffer() {
        munmap(cb_, sizeof(ControlBlock) + sizeof(T) * cb_->cap);
        if constexpr (IsProducer) {
            shm_unlink(shm_name_.c_str());
        }
    }

    // producer appends an item to the buffer tail
    void produce(const T &item) {
        static_assert(IsProducer, "can only be called from producers");
        sem_wait(&cb_->empty);  // blocks if it's full
        assert(cb_->len < cb_->cap);

        sem_wait(&cb_->mutex);
        idx_t tail = (cb_->head + cb_->len) % cb_->cap;
        memcpy(&buffer_[tail], &item, sizeof item);
        cb_->len++;
        sem_post(&cb_->mutex);

        sem_post(&cb_->full);  // signals a blocked consumer, if any
    }

    // consumer retrieves an item from the buffer head
    void consume(T *item) {
        static_assert(!IsProducer, "can only be called from consumers");
        sem_wait(&cb_->full);  // blocks if it's empty
        assert(cb_->len > 0);

        sem_wait(&cb_->mutex);
        memcpy(item, &buffer_[cb_->head], sizeof *item);
        cb_->head = (cb_->head + 1) % cb_->cap;
        cb_->len--;
        sem_post(&cb_->mutex);

        sem_post(&cb_->empty);  // signals the producer if it's been blocked
    }

    const std::string &shm_name() const { return shm_name_; }
    idx_t capacity() const { return cb_->cap; }
    idx_t size() const { return cb_->len; }

private:
    struct ControlBlock {
        sem_t mutex;
        sem_t full;
        sem_t empty;
        idx_t cap;
        idx_t head;
        idx_t len;
    };

    const std::string shm_name_;
    int shm_fd_;
    ControlBlock *cb_;
    T *buffer_;
};
