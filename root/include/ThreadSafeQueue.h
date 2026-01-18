#pragma once
#include <vector>
#include <atomic>
#include <thread>
#include <cstddef>

#include "Util.h"

// Bounded single-producer single-consumer lock-free circular queue.
// Pure spin-based with cpu_relax for efficiency.
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

    virtual ~ThreadSafeQueue() = default;

    ThreadSafeQueue(const ThreadSafeQueue&) = delete;
    ThreadSafeQueue& operator=(const ThreadSafeQueue&) = delete;

    virtual void push(const T& value) {
        if (closed.load(std::memory_order_acquire)) return;

        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;

        // Spin until space available
        while (nextTail == head.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) return;
            util::cpu_relax();
        }

        buf[curTail] = value;
        tail.store(nextTail, std::memory_order_release);
    }

    virtual bool pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);

        // Spin until data available
        while (curHead == tail.load(std::memory_order_acquire)) {
            if (closed.load(std::memory_order_acquire)) {
                return false;
            }
            util::cpu_relax();
        }

        out = buf[curHead];
        size_t nextHead = (curHead + 1) & mask;
        head.store(nextHead, std::memory_order_release);

        return true;
    }

    virtual bool try_push(const T& value) {
        if (closed.load(std::memory_order_acquire)) return false;
        size_t curTail = tail.load(std::memory_order_relaxed);
        size_t nextTail = (curTail + 1) & mask;
        if (nextTail == head.load(std::memory_order_acquire)) return false;
        buf[curTail] = value;
        tail.store(nextTail, std::memory_order_release);
        return true;
    }

    virtual bool try_pop(T& out) {
        size_t curHead = head.load(std::memory_order_relaxed);
        if (curHead == tail.load(std::memory_order_acquire)) return false;
        out = buf[curHead];
        head.store((curHead + 1) & mask, std::memory_order_release);
        return true;
    }

    virtual void shutdown() {
        closed.store(true, std::memory_order_release);
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
    std::atomic<bool> closed;
};