#pragma once

#include <atomic>
#include <new>
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

#define likely(x) __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#ifdef __x86_64__
#define load_fence() asm volatile("lfence" ::: "memory")
#elif __aarch64__
#define load_fence() asm volatile("dmb ishld" ::: "memory")
#else
#define load_fence()
#endif

#ifdef __cpp_lib_hardware_interference_size
#define CACHELINE_ALIGNED alignas(std::hardware_destructive_interference_size)
#else
// 64 bytes on x86-64 | L1_CACHE_BYTES | L1_CACHE_SHIFT | __cacheline_aligned | ...
#define CACHELINE_ALIGNED alignas(64)
#endif

#define CONSUME_SUCCESS 1
#define CONSUME_AGAIN 0
#define CONSUME_FINISHED -1

namespace shm_spmc {

typedef unsigned long idx_t;

struct ShmControlBlock {
    sem_t mutex;
    sem_t full;
    sem_t empty;
    idx_t cap;
    idx_t head;
    idx_t len;
};

// Single-producer multi-consumer bounded buffer using shared memory.
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
        buffer_ = reinterpret_cast<T *>(static_cast<char *>(shmp) + sizeof *cb_);
    }

    ShmControlBlock *cb_;
    T *buffer_;
};

// Single-producer multi-consumer bounded buffer using POSIX shared memory.
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

        size_t shm_size = sizeof *this->cb_ + sizeof(T) * capacity;
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
        munmap(this->cb_, sizeof *this->cb_ + sizeof(T) * this->cb_->cap);
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

// Single-producer multi-consumer bounded buffer using System V shared memory.
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
            size_t shm_size = sizeof *this->cb_ + sizeof(T) * capacity;
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

struct ShmControlBlockLockFree {
    idx_t cap_;
    std::atomic<idx_t> tail_;
    CACHELINE_ALIGNED bool writer_finished_;
};

// The producer operates on the shared tail and the consumers operate on their own local head.
template <typename T, bool IsProducer>
class PShmBBufferLockFree {
public:
    explicit PShmBBufferLockFree(const char *shm_name, idx_t capacity = 0) : shm_name_(shm_name) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        int shm_fd = -1;
        if constexpr (IsProducer) {
            shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
        } else {
            shm_fd = shm_open(shm_name, O_RDONLY, 0600);
        }
        if (shm_fd == -1)
            handle_error("shm_open");

        size_t shm_size = sizeof *cb_ + sizeof(T) * capacity;
        if constexpr (IsProducer) {
            if (ftruncate(shm_fd, shm_size) == -1)
                handle_error("ftruncate");
        } else {
            // consumer can read the capacity from the shared memory
            ssize_t nbytes = read(shm_fd, &capacity, sizeof capacity);
            assert(nbytes == sizeof capacity);
            shm_size = sizeof *cb_ + sizeof(T) * capacity;
        }

        int write_flag = IsProducer ? PROT_WRITE : 0;
        void *shmp = mmap(nullptr, shm_size, PROT_READ | write_flag, MAP_SHARED, shm_fd, 0);
        if (shmp == MAP_FAILED)
            handle_error("mmap");

        // initialize the shared memory control block and buffer pointer
        cb_ = static_cast<ShmControlBlockLockFree *>(shmp);
        if constexpr (IsProducer) {
            cb_->cap_ = capacity;
            // ftruncate() already zeroed the memory, here for clarity
            cb_->tail_.store(0, std::memory_order_relaxed);
            cb_->writer_finished_ = false;
        }
        buffer_ = reinterpret_cast<T *>(static_cast<char *>(shmp) + sizeof *cb_);
    }

    ~PShmBBufferLockFree() {
        if constexpr (IsProducer) {
            // destroys the shared object only when all processes have unmapped it
            shm_unlink(shm_name_.c_str());
            cb_->writer_finished_ = true;
        }
        munmap(cb_, sizeof *cb_ + sizeof(T) * cb_->cap_);
    }

    // producer appends an item to the buffer tail
    // returns false if the buffer is full
    bool produce(const T &item) {
        static_assert(IsProducer, "can only be called from producers");

        idx_t tail = cb_->tail_.load(std::memory_order_relaxed);
        if (tail == cb_->cap_)
            return false;

        memcpy(&buffer_[tail], &item, sizeof item);
        cb_->tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    // consumer retrieves an item from the buffer head
    int consume(T &item) {
        static_assert(!IsProducer, "can only be called from consumers");
        if (cb_->writer_finished_) {
            // insert a memory barrier to prevent speculative loads
            // load_fence();
            if (cb_->tail_.load(std::memory_order_relaxed) == head_)
                return CONSUME_FINISHED;
        } else {
#if 1
            // If the reader is slower than the writer, caching the tail can significantly
            // reduce the # of loads of `tail_`, which the writer updates frequently.
            if (head_ == cached_tail_) {
                cached_tail_ = cb_->tail_.load(std::memory_order_acquire);
                if (head_ == cached_tail_)
                    return CONSUME_AGAIN;
            }
#else
            if (cb_->tail_.load(std::memory_order_acquire) == head_)
                return CONSUME_AGAIN;
#endif
        }

        memcpy(&item, &buffer_[head_], sizeof item);
        head_++;
        return CONSUME_SUCCESS;
    }

    idx_t capacity() const { return cb_->cap_; }

private:
    const std::string shm_name_;
    ShmControlBlockLockFree *cb_;
    T *buffer_;
    idx_t head_ = 0;
    idx_t cached_tail_ = 0;
};

struct ShmControlBlockGiacomoni {
    idx_t cap_;
    bool writer_finished_;
};

// Giacomoni et al. [PPoPP 2008]
// See https://www.youtube.com/watch?v=74QjNwYAJ7M
template <typename T, bool IsProducer>
class PShmBBufferGiacomoni {
public:
    explicit PShmBBufferGiacomoni(const char *shm_name, idx_t capacity = 0) : shm_name_(shm_name) {
        static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");

        int shm_fd = -1;
        if constexpr (IsProducer) {
            shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);
        } else {
            shm_fd = shm_open(shm_name, O_RDONLY, 0600);
        }
        if (shm_fd == -1)
            handle_error("shm_open");

        size_t shm_size = sizeof *cb_ + sizeof *produced_ * capacity + sizeof(T) * capacity;
        if constexpr (IsProducer) {
            // `produced_` array is initialized to null bytes ('\0') by ftruncate
            if (ftruncate(shm_fd, shm_size) == -1)
                handle_error("ftruncate");
        } else {
            // consumer can read the capacity from the shared memory
            ssize_t nbytes = read(shm_fd, &capacity, sizeof capacity);
            assert(nbytes == sizeof capacity);
            shm_size = sizeof *cb_ + sizeof *produced_ * capacity + sizeof(T) * capacity;
        }

        constexpr int flags = IsProducer ? (PROT_READ | PROT_WRITE) : PROT_READ;
        void *shmp = mmap(nullptr, shm_size, flags, MAP_SHARED, shm_fd, 0);
        if (shmp == MAP_FAILED)
            handle_error("mmap");

        cb_ = static_cast<ShmControlBlockGiacomoni *>(shmp);
        if constexpr (IsProducer) {
            cb_->cap_ = capacity;
            cb_->writer_finished_ = false;
        }
        produced_ = reinterpret_cast<std::atomic<bool> *>(&cb_[1]);
        buffer_ = reinterpret_cast<T *>(&produced_[capacity]);
    }

    ~PShmBBufferGiacomoni() {
        if constexpr (IsProducer) {
            shm_unlink(shm_name_.c_str());
            cb_->writer_finished_ = true;
        }
        munmap(produced_, sizeof *cb_ + sizeof *produced_ * cb_->cap_ + sizeof(T) * cb_->cap_);
    }

    // producer appends an item to the buffer tail
    // returns false if the buffer is full
    bool produce(const T &item) {
        static_assert(IsProducer, "can only be called from producers");

        if (tail_ == cb_->cap_)
            return false;

        memcpy(&buffer_[tail_], &item, sizeof item);
        produced_[tail_].store(true, std::memory_order_release);
        tail_++;
        return true;
    }

    // consumer retrieves an item from the buffer head
    int consume(T &item) {
        static_assert(!IsProducer, "can only be called from consumers");

        if (cb_->writer_finished_) {
            if (head_ == cb_->cap_ || !produced_[head_].load(std::memory_order_relaxed))
                return CONSUME_FINISHED;
        } else {
            // no cache coherence protocol overhead unless head_ and tail_ are pointing to
            // the same cache line
            if (!produced_[head_].load(std::memory_order_acquire))
                return CONSUME_AGAIN;
        }

        memcpy(&item, &buffer_[head_], sizeof item);
        head_++;
        return CONSUME_SUCCESS;
    }

    idx_t capacity() const { return cb_->cap_; }

private:
    const std::string shm_name_;

    ShmControlBlockGiacomoni *cb_;
    std::atomic<bool> *produced_;
    T *buffer_;

    // align to cache lines to avoid false sharing if consumers and producers share
    // the same address space (e.g., as different threads of the same process)
    CACHELINE_ALIGNED idx_t head_ = 0;
    CACHELINE_ALIGNED idx_t tail_ = 0;
};

}  // namespace shm_spmc
