#include "TimerManager.hpp"
#include <iostream>

namespace {
    constexpr uint64_t MAX_SECONDS = static_cast<uint64_t>(std::numeric_limits<time_t>::max());

    int create_timerfd(uint64_t delay_ms, uint64_t interval_ms) {
        // 时间值检查（同之前版本）
        if (delay_ms / 1000 > MAX_SECONDS || interval_ms / 1000 > MAX_SECONDS) {
            throw std::invalid_argument("Time value too large");
        }

        int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (tfd == -1) throw std::system_error(errno, std::generic_category(), "timerfd_create");

        struct itimerspec its {
            .it_interval = {
                .tv_sec = static_cast<time_t>(interval_ms / 1000),
                .tv_nsec = static_cast<long>((interval_ms % 1000) * 1000000L)
            },
            .it_value = {
                .tv_sec = static_cast<time_t>(delay_ms / 1000),
                .tv_nsec = static_cast<long>((delay_ms % 1000) * 1000000L)
            }
        };

        if (timerfd_settime(tfd, 0, &its, nullptr) == -1) {
            close(tfd);
            throw std::system_error(errno, std::generic_category(), "timerfd_settime");
        }
        return tfd;
    }
}

TimerManager::TimerManager() {
    epoll_fd_ = epoll_create1(0);
    if (epoll_fd_ == -1) {
        throw std::system_error(errno, std::generic_category(), "epoll_create1");
    }
}

TimerManager::~TimerManager() {
    stop();
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [tfd, _] : timers_) {
        close(tfd);
    }
    close(epoll_fd_);
}

void TimerManager::add_oneshot(uint64_t delay_ms, TimerCallback callback) {
    add_timer(delay_ms, 0, std::move(callback));
}

void TimerManager::add_interval(uint64_t delay_ms, uint64_t interval_ms, TimerCallback callback) {
    if (interval_ms == 0) throw std::invalid_argument("interval_ms must be > 0");
    add_timer(delay_ms, interval_ms, std::move(callback));
}

void TimerManager::add_timer(uint64_t delay_ms, uint64_t interval_ms, TimerCallback callback) {
    int tfd = create_timerfd(delay_ms, interval_ms);

    std::lock_guard<std::mutex> lock(mutex_);
    struct epoll_event ev { .events = EPOLLIN, .data = { .fd = tfd } };
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, tfd, &ev) == -1) {
        close(tfd);
        throw std::system_error(errno, std::generic_category(), "epoll_ctl");
    }

    timers_.emplace(tfd, Timer{ tfd, std::move(callback), interval_ms > 0, interval_ms });
}

void TimerManager::cancel(int timer_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (auto it = timers_.find(timer_id); it != timers_.end()) {
        remove_timer(it->first);
    }
}

void TimerManager::remove_timer(int tfd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, tfd, nullptr);
    close(tfd);
    timers_.erase(tfd);
}

void TimerManager::run() {
    running_ = true;
    constexpr int MAX_EVENTS = 10;
    struct epoll_event events[MAX_EVENTS];

    while (running_) {
        int n = epoll_wait(epoll_fd_, events, MAX_EVENTS, -1);
        if (n == -1) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
        }

        std::unique_lock<std::mutex> lock(mutex_);
        for (int i = 0; i < n && running_; ++i) {
            int tfd = events[i].data.fd;
            uint64_t expirations;
            read(tfd, &expirations, sizeof(expirations));

            if (auto it = timers_.find(tfd); it != timers_.end()) {
                auto callback = it->second.callback;  // 复制回调（避免死锁）
                lock.unlock();
                callback();  // 在锁外执行回调
                lock.lock();

                if (!it->second.is_interval) {
                    remove_timer(tfd);
                }
            }
        }
    }
}

void TimerManager::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
}
