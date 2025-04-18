#include "TimerManager.hpp"
#include <thread>
#include <iostream>

int main() {
    TimerManager tm;

    // 线程1：添加定时器
    std::thread t1([&] {
        tm.add_interval(1000, 1000, [] {
            std::cout << "Timer 1 (thread " << std::this_thread::get_id() << ")\n";
        });
    });

    // 线程2：添加另一个定时器
    std::thread t2([&] {
        tm.add_oneshot(3000, [] {
            std::cout << "Timer 2 (thread " << std::this_thread::get_id() << ")\n";
        });
    });

    // 主线程运行事件循环
    std::thread t3([&] {
        tm.run();
    });

    t1.join();
    t2.join();
    std::this_thread::sleep_for(std::chrono::seconds(5));
    tm.stop();  // 安全停止
    t3.join();
}
