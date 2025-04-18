#pragma once

#include <functional>
#include <unordered_map>
#include <mutex>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <system_error>
#include <limits>

class TimerManager {
public:
    using TimerCallback = std::function<void()>;

    TimerManager();
    ~TimerManager();

    // 禁用拷贝和移动
    TimerManager(const TimerManager&) = delete;
    TimerManager& operator=(const TimerManager&) = delete;

    // 线程安全的接口
    void add_oneshot(uint64_t delay_ms, TimerCallback callback);
    void add_interval(uint64_t delay_ms, uint64_t interval_ms, TimerCallback callback);
    void cancel(int timer_id);
    void run();
    void stop();  // 新增：安全停止事件循环

private:
    struct Timer {
        int tfd;
        TimerCallback callback;
        bool is_interval;
        uint64_t interval_ms;
    };

    int epoll_fd_;
    std::unordered_map<int, Timer> timers_;
    bool running_ = false;
    std::mutex mutex_;  // 保护共享资源

    void add_timer(uint64_t delay_ms, uint64_t interval_ms, TimerCallback callback);
    void remove_timer(int tfd);
};
