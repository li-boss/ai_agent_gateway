// ============================================================================
// [NEW ADDITION AREA - START]
// 新增部分：添加了 /status 负载监控路由和 /agent/cancel 协同取消路由，
//           在类中定义了 cancelled_tasks_ 集合与 cancel_mutex_ 互斥锁，
//           并在 ThreadPool 任务执行的前后添加了协同取消逻辑（双重检查），
//           从而支持会话的强行中断与监控指标导出。
// 修改日期：2026-06-14
// 范围：第一行至下面的 [NEW ADDITION AREA - END]
// ============================================================================

#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <thread>
#include <string>
#include <set>
#include <mutex>
#include "ThreadPool.h"
#include "MemoryStorage.h"
#include "LLMClient.h"
#include "WebhookClient.h"
#include "Logger.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

class HttpServer {
public:
    HttpServer(ThreadPool& pool, MemoryStorage& storage,
               LLMClient& llm, WebhookClient& webhook)
        : pool_(pool), storage_(storage), llm_(llm), webhook_(webhook) {}

    ~HttpServer() {
        stop();
    }

    void start(int port) {
        // Register POST route for receiving tasks
        svr_.Post("/agent/task", [this](const httplib::Request& req, httplib::Response& res) {
            handleTask(req, res);
        });

        // Register POST route for cancelling tasks cooperatively
        svr_.Post("/agent/cancel", [this](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                int taskId = body.at("taskId").get<int>();
                {
                    std::lock_guard<std::mutex> lock(cancel_mutex_);
                    cancelled_tasks_.insert(taskId);
                    LOG_INFO << "HttpServer registered cancel request for task " << taskId;
                }
                res.status = 200;
                res.set_content("{\"status\":\"cancelled\"}", "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid input JSON\"}", "application/json");
            }
        });

        // Register GET route for engine load monitoring telemetry
        svr_.Get("/status", [this](const httplib::Request&, httplib::Response& res) {
            json reply = {
                {"queue_size", pool_.get_queue_size()},
                {"active_threads", pool_.get_active_workers()},
                {"total_threads", pool_.get_total_workers()}
            };
            res.status = 200;
            res.set_content(reply.dump(), "application/json");
        });

        // Register GET route for engine status ping
        svr_.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("pong", "text/plain");
        });

        // Start listening in a background thread to prevent blocking main thread
        server_thread_ = std::thread([this, port]() {
            LOG_INFO << "HttpServer starting on 0.0.0.0:" << port;
            if (!svr_.listen("0.0.0.0", port)) {
                LOG_ERROR << "HttpServer failed to listen on port " << port;
            }
        });
    }

    void stop() {
        svr_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        LOG_INFO << "HttpServer stopped.";
    }

private:
    void handleTask(const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            int taskId = body.at("taskId").get<int>();
            int userId = body.at("userId").get<int>();
            std::string prompt = body.at("prompt").get<std::string>();
            int npcId = body.at("npcId").get<int>();

            LOG_INFO << "HttpServer received task: taskId=" << taskId << ", userId=" << userId << ", npcId=" << npcId;

            // Enqueue task to ThreadPool
            pool_.enqueue([this, taskId, userId, prompt, npcId]() {
                // 协同取消检查点 1：排队唤醒后立即检查
                {
                    std::lock_guard<std::mutex> lock(cancel_mutex_);
                    if (cancelled_tasks_.count(taskId)) {
                        LOG_INFO << "[Worker] Task " << taskId << " was cancelled before execution starts. Aborting.";
                        cancelled_tasks_.erase(taskId);
                        return;
                    }
                }

                // Sleep 2 seconds as requested by Prompt 3: "Worker 线程沉睡 2 秒（模拟大模型推理）"
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // 协同取消检查点 2：模拟时延后与 LLM 请求前再次检查
                {
                    std::lock_guard<std::mutex> lock(cancel_mutex_);
                    if (cancelled_tasks_.count(taskId)) {
                        LOG_INFO << "[Worker] Task " << taskId << " was cancelled during sleep delay. Aborting.";
                        cancelled_tasks_.erase(taskId);
                        return;
                    }
                }

                // Get memory context
                std::string history = storage_.get_memory(userId);

                // Call LLMClient
                LOG_INFO << "[Worker] Requesting LLM for task " << taskId;
                PayloadMessage response = llm_.request(prompt, history);

                // 协同取消检查点 3：LLM 推理结束但还没回调或写入内存前检查
                {
                    std::lock_guard<std::mutex> lock(cancel_mutex_);
                    if (cancelled_tasks_.count(taskId)) {
                        LOG_INFO << "[Worker] Task " << taskId << " was cancelled after LLM completion. Discarding.";
                        cancelled_tasks_.erase(taskId);
                        return;
                    }
                }

                std::string result;
                if (response.success) {
                    storage_.append_memory(userId, "User:" + prompt + "\nAI:" + response.content);
                    result = response.content;
                    LOG_INFO << "[Worker] Task " << taskId << " response success.";
                } else {
                    result = "Error: " + response.content;
                    LOG_ERROR << "[Worker] Task " << taskId << " response failed: " << response.content;
                }

                // Notify back to control plane
                webhook_.notifyResult(taskId, result);
            });

            // Immediately return HTTP 200 { "status": "accepted" }
            json reply = { {"status", "accepted"} };
            res.status = 200;
            res.set_content(reply.dump(), "application/json");

        } catch (const std::exception& e) {
            LOG_ERROR << "HttpServer handleTask error: " << e.what();
            json reply = { {"error", e.what()} };
            res.status = 400;
            res.set_content(reply.dump(), "application/json");
        }
    }

    ThreadPool& pool_;
    MemoryStorage& storage_;
    LLMClient& llm_;
    WebhookClient& webhook_;
    httplib::Server svr_;
    std::thread server_thread_;

    // 存储被取消的任务 ID
    std::set<int> cancelled_tasks_;
    std::mutex cancel_mutex_;
};

#endif // HTTP_SERVER_H

// ============================================================================
// [NEW ADDITION AREA - END]
// ============================================================================


// ============================================================================
// 以下为原文件内容，已注释保留以防直接删除：
// ============================================================================
/*
#ifndef HTTP_SERVER_H
#define HTTP_SERVER_H

#include <thread>
#include <string>
#include "ThreadPool.h"
#include "MemoryStorage.h"
#include "LLMClient.h"
#include "WebhookClient.h"
#include "Logger.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

class HttpServer {
public:
    HttpServer(ThreadPool& pool, MemoryStorage& storage,
               LLMClient& llm, WebhookClient& webhook)
        : pool_(pool), storage_(storage), llm_(llm), webhook_(webhook) {}

    ~HttpServer() {
        stop();
    }

    void start(int port) {
        // Register POST route for receiving tasks
        svr_.Post("/agent/task", [this](const httplib::Request& req, httplib::Response& res) {
            handleTask(req, res);
        });

        // Register GET route for engine status ping
        svr_.Get("/ping", [](const httplib::Request&, httplib::Response& res) {
            res.set_content("pong", "text/plain");
        });

        // Start listening in a background thread to prevent blocking main thread
        server_thread_ = std::thread([this, port]() {
            LOG_INFO << "HttpServer starting on 0.0.0.0:" << port;
            if (!svr_.listen("0.0.0.0", port)) {
                LOG_ERROR << "HttpServer failed to listen on port " << port;
            }
        });
    }

    void stop() {
        svr_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
        LOG_INFO << "HttpServer stopped.";
    }

private:
    void handleTask(const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            int taskId = body.at("taskId").get<int>();
            int userId = body.at("userId").get<int>();
            std::string prompt = body.at("prompt").get<std::string>();
            int npcId = body.at("npcId").get<int>();

            LOG_INFO << "HttpServer received task: taskId=" << taskId << ", userId=" << userId << ", npcId=" << npcId;

            // Enqueue task to ThreadPool
            pool_.enqueue([this, taskId, userId, prompt, npcId]() {
                // Sleep 2 seconds as requested by Prompt 3: "Worker 线程沉睡 2 秒（模拟大模型推理）"
                std::this_thread::sleep_for(std::chrono::seconds(2));

                // Get memory context
                std::string history = storage_.get_memory(userId);

                // Call LLMClient
                LOG_INFO << "[Worker] Requesting LLM for task " << taskId;
                PayloadMessage response = llm_.request(prompt, history);

                std::string result;
                if (response.success) {
                    storage_.append_memory(userId, "User:" + prompt + "\nAI:" + response.content);
                    result = response.content;
                    LOG_INFO << "[Worker] Task " << taskId << " response success.";
                } else {
                    result = "Error: " + response.content;
                    LOG_ERROR << "[Worker] Task " << taskId << " response failed: " << response.content;
                }

                // Notify back to control plane
                webhook_.notifyResult(taskId, result);
            });

            // Immediately return HTTP 200 { "status": "accepted" }
            json reply = { {"status", "accepted"} };
            res.status = 200;
            res.set_content(reply.dump(), "application/json");

        } catch (const std::exception& e) {
            LOG_ERROR << "HttpServer handleTask error: " << e.what();
            json reply = { {"error", e.what()} };
            res.status = 400;
            res.set_content(reply.dump(), "application/json");
        }
    }

    ThreadPool& pool_;
    MemoryStorage& storage_;
    LLMClient& llm_;
    WebhookClient& webhook_;
    httplib::Server svr_;
    std::thread server_thread_;
};

#endif // HTTP_SERVER_H
*/
