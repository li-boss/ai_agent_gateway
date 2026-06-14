// ============================================================================
// [NEW ADDITION AREA - START]
// 新增部分：本段新加代码包含网关的 HTTP 接口扩展、信号量处理逻辑，以及生产级网关启动入口。
// 修改日期：2026-06-14
// 职责：
//   1. 引入 core/HttpServer.h 与 core/WebhookClient.h。
//   2. 增加信号捕捉（SIGINT, SIGTERM）实现优雅关闭。
//   3. 读取环境变量 DEEPSEEK_API_KEY、CONTROL_HOST、CONTROL_PORT 初始化服务并启动。
// 范围：第一行至下面的 [NEW ADDITION AREA - END]
// ============================================================================

#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <csignal>
#include <mutex>
#include <condition_variable>

#include "core/ThreadPool.h"
#include "core/MemoryStorage.h"
#include "core/StorageInterface.h"
#include "core/LLMClient.h"
#include "core/Logger.h"
#include "core/HttpServer.h"
#include "core/WebhookClient.h"

namespace {
    std::mutex signal_mtx;
    std::condition_variable signal_cv;
    bool should_stop = false;
}

// 信号处理函数，用于优雅停机
void signal_handler(int signal) {
    if (signal == SIGINT || signal == SIGTERM) {
        {
            std::lock_guard<std::mutex> lock(signal_mtx);
            should_stop = true;
        }
        signal_cv.notify_one();
    }
}

int main() {
    LOG_INFO << "========== AI Agent 异步网关启动 ==========";
    
    // 1. 读取环境变量 DEEPSEEK_API_KEY（不存在则报错退出）
    const char* env_p = std::getenv("DEEPSEEK_API_KEY");
    if (env_p == nullptr) {
        LOG_ERROR << "[Fatal Error] 缺失环境变量 DEEPSEEK_API_KEY。";
        LOG_ERROR << "请在运行前执行: export DEEPSEEK_API_KEY=\"你的真实Key\"";
        return 1; // 异常退出状态码
    }
    std::string api_key = env_p;

    // 2. 读取环境变量 CONTROL_HOST 和 CONTROL_PORT
    const char* host_p = std::getenv("CONTROL_HOST");
    std::string control_host = (host_p != nullptr) ? host_p : "localhost";

    const char* port_p = std::getenv("CONTROL_PORT");
    int control_port = (port_p != nullptr) ? std::atoi(port_p) : 3000;

    // 3. 创建核心服务组件
    ThreadPool pool(4);
    MemoryStorage storage(100);
    LLMClient llm(api_key);
    WebhookClient webhook(control_host, control_port);
    HttpServer httpServer(pool, storage, llm, webhook);

    // 4. 注册信号处理程序
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 5. 启动 HTTP 服务，监听端口 8080
    httpServer.start(8080);
    LOG_INFO << "引擎节点启动成功，监听端口 8080";

    // 6. 阻塞等待信号触发优雅停机
    {
        std::unique_lock<std::mutex> lock(signal_mtx);
        signal_cv.wait(lock, [] { return should_stop; });
    }

    LOG_INFO << "收到关闭信号，正在关闭引擎节点，请稍候...";
    httpServer.stop();
    LOG_INFO << "引擎节点优雅停机完成。";

    return 0; 
}

// ============================================================================
// [NEW ADDITION AREA - END]
// ============================================================================


// ============================================================================
// 以下为原文件内容，已注释保留以防直接删除：
// ============================================================================
/*
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <random>
#include "core/ThreadPool.h"
#include "core/MemoryStorage.h"
#include "core/StorageInterface.h"
#include "core/LLMClient.h"
#include <cstdlib>
#include "core/Logger.h"
// 这是一个模拟的耗时操作：假设我们在请求外部的大模型 API
std::string real_llm_api_call(int user_id, const std::string& prompt,StorageInterface& db_interface,LLMClient &llm_client) {
   
    //查阅历史
    std::string history=db_interface.get_memory(user_id);
    bool is_new_user=history.empty();

    LOG_INFO<< "[Worker " << std::this_thread::get_id() << "] 正在为用户 " << user_id 
              << (is_new_user ? " [新访客]" : " [老熟人]") 
              << " 请求 DeepSeek 大模型 (这可能需要几秒钟)..." ;
   
    //如果有记忆，打印检查
    if(!history.empty()){
        LOG_INFO<<"   -> 发现用户 " << user_id << " 的历史记忆:\n"<<history;
    }else{
       LOG_INFO<< "   -> 用户 " << user_id << " 是新访客，暂无记忆。";
    }
    
    // 第二步：发起真实的 HTTP 网络请求 (这里会发生网络阻塞，但被限制在子线程里)
    // 传入当前的 prompt 和刚查出来的 history
   
    PayloadMessage ai_response = llm_client.request(prompt, history);


    // 核心防御：只有请求真正成功，才把记忆写入底层引擎！
if (ai_response.success) {
   db_interface.append_memory(user_id, "User:" + prompt + "\nAI:" + ai_response.content);
    return "\n>>> 用户 " + std::to_string(user_id) + " 的 AI 回复:\n" + ai_response.content + "\n";
} else {
    // 失败时，返回报错信息给用户，但绝对不写入历史记忆
    return "\n>>> 用户 " + std::to_string(user_id) + " 请求失败: " + ai_response.content + "\n";
}
}

int main() {
    LOG_INFO << "========== AI Agent 异步网关启动 ==========";
    
    // 尝试从操作系统读取名为 "DEEPSEEK_API_KEY" 的环境变量
    const char* env_p = std::getenv("DEEPSEEK_API_KEY");
    
// 安全拦截：如果没有读取到密钥，直接终止程序
    if (env_p == nullptr) {
        LOG_ERROR << "[Fatal Error] 缺失环境变量 DEEPSEEK_API_KEY。";
        LOG_ERROR<< "请在运行前执行: export DEEPSEEK_API_KEY=\"你的真实Key\"" ;
        return 1; // 异常退出状态码
    }

    std::string my_api_key = env_p;

    // 1. 启动我们的手写引擎：雇佣 4 个打工人
    ThreadPool pool(4);
    MemoryStorage memory_backend(1);
    LLMClient global_llm_client(my_api_key);
    
    // 2. 用一个数组，把所有请求的“取餐小票 (future)”保存起来
    std::vector<std::future<std::string>> results;

    auto start_time = std::chrono::high_resolution_clock::now();

    LOG_INFO<< "--- [发送真实请求] 2个用户同时向 DeepSeek 提问 ---\n";
    
    // 3. 派发任务：注意看这里传参的变化！
    // 我们必须用 std::ref 同时把 memory 和 llm_client 的引用传给打工人
    results.emplace_back(pool.enqueue(
        real_llm_api_call, 1, "用 C++ 写一个最简单的 Hello World 程序。", 
        std::ref(static_cast<StorageInterface&>(memory_backend)), std::ref(global_llm_client)
    ));

    results.emplace_back(pool.enqueue(
        real_llm_api_call, 2, "请用一句话解释什么是黑洞。", 
        std::ref(static_cast<StorageInterface&>(memory_backend)), std::ref(global_llm_client)
    ));

    // 4. 阻塞等待：此时主线程卡在这里，等待网卡收发数据
    for(size_t i = 0; i < results.size(); ++i) {
        std::cout << results[i].get() << std::endl;
    }
    

    auto round1_time = std::chrono::high_resolution_clock::now();
    LOG_INFO << "========== 第一轮请求处理完毕！ ==========\n\n";

    LOG_INFO<< "--- [触发多轮对话] 2个用户基于刚才的记忆继续追问 ---";
    results.clear(); // 清空旧的取餐小票

    // 用户 1 刚才问了 C++ 的 Hello World，现在让他问 Python
    results.emplace_back(pool.enqueue(real_llm_api_call,1,"不用 C++ 了，帮我把上面的代码改成 Python 版本。",std::ref(static_cast<StorageInterface&>(memory_backend)),std::ref(global_llm_client)));
    
    // 用户 2 刚才问了黑洞，现在让他继续追问
    results.emplace_back(pool.enqueue(
        real_llm_api_call, 2, "那么它未来会把地球也吸进去吗？", 
        std::ref(static_cast<StorageInterface&>(memory_backend)), std::ref(global_llm_client)
    ));

    //再次阻塞等待第二轮完成
    for(size_t i=0;i<results.size();++i){
        std::cout<<results[i].get()<<std::endl;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end_time - start_time;
   LOG_INFO<< "========== 所有网络请求处理完毕！总耗时: " << diff.count() << " 秒 ==========";

    return 0; // 退出 main 函数时，pool 的析构函数会被自动调用，优雅关机
}
*/