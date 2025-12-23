#pragma once
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <condition_variable>

// Bounded SPSC queue with spin-then-event fallback.
// - Single producer, single consumer only.
// - Capacity rounded up to next power-of-two; usable slots = capacity - 1.
// - Short spin/yield for low-latency common case; fallback to condition_variable for longer waits.
// - Provides try_push/try_pop and size()/capacity() helpers for diagnostics.
// - Supports shutdown() to unblock consumer/producer safely.
template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 16384) {
        size_t cap = 1;
        while (cap < capacity) cap <<= 1;
        if (cap < 2) cap = 2;
        buf.resize(cap);
        mask = cap - 1;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        closed.store(false, std::memory_order_relaxed);
    }

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    // Producer: push value. Blocks (spins then waits) if full.
    // If queue has been shutdown, push will return without inserting.
    void push(const T& value) {
        // If shutdown, drop push (no-op). Producer must handle this semantics.
        if (closed.load(std::memory_order_acquire)) return;

        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;

        // Short spin/yield period
        size_t spin = 0;
        while (nextTail == head.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) return; // shutdown while waiting
            if (++spin < 128) {
                std::this_thread::yield();
            }
            else {
                // Fall back to blocking wait
                std::unique_lock<std::mutex> lock(mutex_);
                not_full_cv_.wait(lock, [&] {
                    return closed.load(std::memory_order_acquire) ||
                           (((tail.load(std::memory_order_relaxed) + 1) & mask) != head.load(std::memory_order_acquire));
                });
                if (closed.load(std::memory_order_acquire)) return; // shutdown while waiting
                curTail = tail.load(std::memory_order_relaxed);
                nextTail = (curTail + 1) & mask;
                spin = 0;
            }
        }

        buf[curTail] = value;
        // publish
        tail.store(nextTail, std::memory_order_release);

        // Notify consumer waiting for not-empty
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_empty_cv_.notify_one();
        }
    }

    // Blocking pop into out. Returns true if an element was produced into out.
    // Returns false if queue was shutdown and empty (no more items).
    bool pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);

        size_t spin = 0;
        while (curHead == tail.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) {
                // no more items will arrive
                return false;
            }
            if (++spin < 128) {
                std::this_thread::yield();
            } else {
                std::unique_lock<std::mutex> lock(mutex_);
                not_empty_cv_.wait(lock, [&] {
                    return closed.load(std::memory_order_acquire) || (head.load(std::memory_order_relaxed) != tail.load(std::memory_order_acquire));
                });
                if (closed.load(std::memory_order_acquire) && head.load(std::memory_order_relaxed) == tail.load(std::memory_order_acquire)) {
                    return false;
                }
                curHead = head.load(std::memory_order_relaxed);
                spin = 0;
            }
        }

        out = buf[curHead];
        size_t nextHead = (curHead + 1) & mask;
        head.store(nextHead, std::memory_order_release);

        // Notify producer waiting for not-full
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_full_cv_.notify_one();
        }

        return true;
    }

    // Non-blocking attempt to push. Returns true on success.
    bool try_push(const T& value) {
        if (closed.load(std::memory_order_acquire)) return false;
        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;
        if (nextTail == head.load(std::memory_order_acquire)) return false; // full
        buf[curTail] = value;
        tail.store(nextTail, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_empty_cv_.notify_one();
        }
        return true;
    }

    // Non-blocking attempt to pop. Returns true on success.
    bool try_pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);
        if (curHead == tail.load(std::memory_order_acquire)) return false; // empty
        out = buf[curHead];
        head.store((curHead + 1) & mask, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_full_cv_.notify_one();
        }
        return true;
    }

    // Shutdown the queue: unblocks any waiting pop/push calls and prevents future pushes.
    void shutdown() {
        closed.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_empty_cv_.notify_all();
            not_full_cv_.notify_all();
        }
    }

    bool isShutdown() const noexcept {
        return closed.load(std::memory_order_acquire);
    }

    // Approximate size (not atomic-consistent across producers/consumers, for diagnostics only)
    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        return (t - h) & mask;
    }

    // Usable capacity (buf.size() - 1)
    size_t capacity() const {
        return buf.size() - 1;
    }

private:
    std::vector<T> buf;
    size_t mask;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;

    // For fallback blocking
    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    std::atomic<bool> closed;
};