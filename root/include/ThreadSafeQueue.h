#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>

template <typename T>
class ThreadSafeQueue {
public:
    void push(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        q.push(value);
        cv.notify_one();
    }

    T pop() {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&] { return !q.empty(); });
        T val = q.front();
        q.pop();
        return val;
    }

private:
    std::queue<T> q;
    std::mutex mtx;
    std::condition_variable cv;
};
