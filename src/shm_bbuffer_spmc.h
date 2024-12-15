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
#include <sys/shm.h>
#include <unistd.h>

#define handle_error(msg)   \
    do {                    \
        perror(msg);        \
        exit(EXIT_FAILURE); \
    } while (0)

namespace shm_spmc {

typedef unsigned idx_t;
struct ShmControlBlock {
    sem_t mutex;
    sem_t full;
    sem_t empty;
    idx_t cap;
    idx_t head;
    idx_t len;
};

// Single-producer multiple-consumer bounded buffer using shared memory.
// see also https://en.wikipedia.org/wiki/Producer-consumer_problem
template <typename T, bool IsProducer>
class ShmCircularBufferBase {
protected:
    ShmCircularBufferBase() = default;

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

    idx_t capacity() const { return cb_->cap; }
    idx_t size() const { return cb_->len; }

    void init_shm_meta(void *shmp, idx_t capacity) {
        cb_ = static_cast<ShmControlBlock *>(shmp);
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
        buffer_ = reinterpret_cast<T *>(static_cast<char *>(shmp) + sizeof(ShmControlBlock));
    }

    ShmControlBlock *cb_;
    T *buffer_;
};

// Single-producer multiple-consumer bounded buffer using POSIX shared memory.
template <typename T, bool IsProducer>
class PShmCircularBuffer : ShmCircularBufferBase<T, IsProducer> {
    using base_ = ShmCircularBufferBase<T, IsProducer>;

public:
    explicit PShmCircularBuffer(const char *shm_name, idx_t capacity) : shm_name_(shm_name) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        int shm_fd = -1;
        if constexpr (IsProducer) {
            shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
        } else {
            shm_fd = shm_open(shm_name, O_RDWR, 0600);
        }
        if (shm_fd == -1)
            handle_error("shm_open");

        size_t shm_size = sizeof(ShmControlBlock) + sizeof(T) * capacity;
        if constexpr (IsProducer) {
            if (ftruncate(shm_fd, shm_size) == -1)
                handle_error("ftruncate");
        }

        // We cannot use huges pages (MAP_HUGETLB) here as POSIX share memory objects are
        // created in a tmpfs filesystem (/dev/shm) but a hugetlbfs fd is required (or the
        // MAP_ANONYMOUS flag is set).
        // A simple solution is to `open()` a hugetlbfs file and `mmap()` it as shared.
        void *shmp = mmap(nullptr, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (shmp == MAP_FAILED)
            handle_error("mmap");

        this->init_shm_meta(shmp, capacity);
    }

    ~PShmCircularBuffer() {
        munmap(this->cb_, sizeof(ShmControlBlock) + sizeof(T) * this->cb_->cap);
        if constexpr (IsProducer) {
            shm_unlink(shm_name_.c_str());
        }
    }

    const std::string &shm_name() const { return shm_name_; }

    using base_::consume;
    using base_::produce;
    using base_::capacity;
    using base_::size;

private:
    const std::string shm_name_;
};

// Single-producer multiple-consumer bounded buffer using System V shared memory.
template <typename T, bool IsProducer>
class SVShmCircularBuffer : ShmCircularBufferBase<T, IsProducer> {
    using base_ = ShmCircularBufferBase<T, IsProducer>;

public:
    // Pass `key` for the producer, pass `shm_id` (via `ipcs -m`) for consumers.
    explicit SVShmCircularBuffer(key_t key, idx_t capacity, int shm_id = -1,
                                 bool use_huge_pages = false)
        : shm_id_(shm_id) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
        // Large pages can reduce TLB misses as the pages referenced by the process now become
        // a smaller set (less TLB pressure). Also, the address translation becomes faster.
        // For example, 4KB -> 2MB pages, 4-level -> 3-level page table walks.
        // But the injudicious use of huge pages can also lead to memory fragmentation.
        //
        // To see huge pages info (usage, size, etc.):
        //      cat /proc/meminfo | grep Huge
        // To enable N (=128) huge pages on Linux:
        //      echo 128 | sudo tee /proc/sys/vm/nr_hugepages
        // To add your group id to `hugetlb_shm_group`:
        //      echo `id -g $(whoami)` | sudo tee /proc/sys/vm/hugetlb_shm_group
        // See more at https://www.kernel.org/doc/html/latest/admin-guide/mm/hugetlbpage.html
        if constexpr (IsProducer) {
            size_t shm_size = sizeof(ShmControlBlock) + sizeof(T) * capacity;
            int huge_tlb_flag = use_huge_pages ? SHM_HUGETLB : 0;
            shm_id_ = shmget(key, shm_size, huge_tlb_flag | IPC_CREAT | IPC_EXCL | 0600);
            if (shm_id_ == -1)
                handle_error("shmget");
        }

        void *shmp = shmat(shm_id_, nullptr, 0);
        if (shmp == (void *)-1)
            handle_error("shmat");

        this->init_shm_meta(shmp, capacity);
    }

    ~SVShmCircularBuffer() {
        shmdt(this->cb_);
        if constexpr (IsProducer) {
            shmctl(shm_id_, IPC_RMID, nullptr);
        }
    }

    int shm_id() const { return shm_id_; }

    using base_::consume;
    using base_::produce;
    using base_::capacity;
    using base_::size;

private:
    int shm_id_;
};

}  // namespace shm_spmc
