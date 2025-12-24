#pragma once
#include <vector>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <condition_variable>

// Bounded single-producer single-consumer circular queue with spin-then-condition-variable fallback.
// Supports push/try_push/pop/try_pop, shutdown(), size() and capacity().
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

    void push(const T& value) {
        if (closed.load(std::memory_order_acquire)) return;

        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;

        size_t spin = 0;
        while (nextTail == head.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) return;
            if (++spin < 128) {
                std::this_thread::yield();
            } else {
                std::unique_lock<std::mutex> lock(mutex_);
                not_full_cv_.wait(lock, [&] {
                    return closed.load(std::memory_order_acquire) ||
                           (((tail.load(std::memory_order_relaxed) + 1) & mask) != head.load(std::memory_order_acquire));
                });
                if (closed.load(std::memory_order_acquire)) return;
                curTail = tail.load(std::memory_order_relaxed);
                nextTail = (curTail + 1) & mask;
                spin = 0;
            }
        }

        buf[curTail] = value;
        tail.store(nextTail, std::memory_order_release);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_empty_cv_.notify_one();
        }
    }

    bool pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);

        size_t spin = 0;
        while (curHead == tail.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) {
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

        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_full_cv_.notify_one();
        }

        return true;
    }

    bool try_push(const T& value) {
        if (closed.load(std::memory_order_acquire)) return false;
        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;
        if (nextTail == head.load(std::memory_order_acquire)) return false;
        buf[curTail] = value;
        tail.store(nextTail, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_empty_cv_.notify_one();
        }
        return true;
    }

    bool try_pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);
        if (curHead == tail.load(std::memory_order_acquire)) return false;
        out = buf[curHead];
        head.store((curHead + 1) & mask, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            not_full_cv_.notify_one();
        }
        return true;
    }

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

    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        return (t - h) & mask;
    }

    size_t capacity() const {
        return buf.size() - 1;
    }

private:
    std::vector<T> buf;
    size_t mask;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;

    mutable std::mutex mutex_;
    std::condition_variable not_empty_cv_;
    std::condition_variable not_full_cv_;

    std::atomic<bool> closed;
};