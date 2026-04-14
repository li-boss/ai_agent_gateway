#ifndef LOGGER_H
#define LOGGER_H

#include <iostream>
#include <sstream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <fstream>

// 全局的日志锁，保证多线程同时打日志时，控制台输出不会错乱串行
inline std::mutex& get_log_mutex() {
    static std::mutex mtx;
    return mtx;
}

class LogMessage {
public:
    // 构造函数：每一条日志产生时，先自动加上时间戳和级别
    LogMessage(const char* level, const char* color_code) {
        // 获取当前系统时间
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
#ifdef _WIN32
        localtime_s(&tm_buf, &now_time_t);
#else
        localtime_r(&now_time_t, &tm_buf);
#endif
        // 拼装前缀：[时间] [级别]
        os << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "] "
           << color_code << "[" << level << "]\033[0m "; // \033[0m 是重置颜色的终端控制码
    }

    ~LogMessage() {
        os << "\n"; 
        std::string log_str = os.str(); // 把流里的字符串取出来

        std::lock_guard<std::mutex> lock(get_log_mutex());
        
        // 1. 打印到屏幕 (依然保留)
        std::cout << log_str;

        // 2. 写入到本地硬盘文件 (双写黑魔法)
        // 使用 static 保证文件流只在第一次调用时打开，并且使用 ios::app (追加模式)
        static std::ofstream log_file("gateway.log", std::ios::app);
        if (log_file.is_open()) {
            log_file << log_str;
            log_file.flush(); // 强制立刻落盘，防止程序突然崩溃导致日志还在内存里没写进去
        }
    }

    // 提供给外部的流接口
    std::ostringstream& stream() { return os; }

private:
    std::ostringstream os; // 内部的字符缓冲区
};

// 定义宏，模拟出工业级日志库的调用风格 (包含 ANSI 终端颜色码)
#define LOG_INFO  LogMessage("INFO",  "\033[32m").stream()   // 绿色
#define LOG_WARN  LogMessage("WARN",  "\033[33m").stream()   // 黄色
#define LOG_ERROR LogMessage("ERROR", "\033[31m").stream()   // 红色

#endif // LOGGER_H