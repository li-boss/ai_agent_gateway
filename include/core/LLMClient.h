#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <string>
#include <iostream>

// 宏定义必须放在 include httplib 之前，用于开启 HTTPS 支持
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

// 引入这个结构体作为“防腐层”，隔离底层的 HTTP 状态和 JSON 库
struct PayloadMessage{
    bool success;           // 请求是否真正成功
    std::string content;    // 成功时存放 AI 回复，失败时存放错误原因
    int status_code;        // HTTP 状态码，方便上层排查
    int total_tokens;       // 记录消耗的 Token（可选扩展）
};

class LLMClient{

public:
        // 初始化时传入 API Key。默认对接 DeepSeek 的 API 地址，因为其完全兼容 OpenAI 标准
        LLMClient(const std::string& api_key):api_key_(api_key){
            host_="api.deepseek.com";
            path_="/chat/completions";
        }

        //核心接口：接收当前用户的 Prompt 和历史记忆，返回大模型的回答
        PayloadMessage request(const std::string&user_prompt,const std::string&history){

            //创建HTTPS客户端
            httplib::Client cli("https://"+host_);

            //健壮性设置:限制连接超时和读取超时，防止工作线程被无限期挂起(卡死)
            cli.set_connection_timeout(5);
            cli.set_read_timeout(30);

            //构造符合 OpenAI接口规范的 JSON 请求体
            json payload={
                {"model","deepseek-chat"},
                {"messages",json::array()},
                {"temperature",0.7}
            };

            //1.如果有历史记忆，将其作为system角色注入上下文
            if(!history.empty()){
                payload["messages"].push_back({
                    {"role","system"},
                    {"content","以下是该用户的历史记忆，请严格结合上下文语境回答新问题:\n"+history}
                });
            }

            //2.注入当前用户的最新提问
            payload["messages"].push_back({
                {"role","user"},
                {"content",user_prompt}
            });

            // 构造 HTTP Headers，用于身份鉴权和声明数据格式
            httplib::Headers headers={
                {"Authorization", "Bearer " + api_key_},
                {"Content-Type", "application/json"}
            };

            // 发起同步 POST 请求。payload.dump() 的作用是将 JSON 对象序列化为 std::string
            auto res=cli.Post(path_,headers,payload.dump(),"application/json");

            PayloadMessage result; // 准备返回的对象

            //异常处理
            if(res) {
        result.status_code = res->status;
        if(res->status == 200) {
            try {
                json response_json = json::parse(res->body);
                // 安全提取字段，并标记成功
                result.success = true;
                result.content = response_json["choices"][0]["message"]["content"].get<std::string>();
                // 可选：提取 Token 消耗
                // result.total_tokens = response_json.value("usage", json::object()).value("total_tokens", 0);
                return result;
            } catch(const json::parse_error &e) {
                result.success = false;
                result.content = "JSON 解析失败: " + std::string(e.what());
                return result;
            }
        } else {
            result.success = false;
            result.content = "API 返回异常状态码: " + std::to_string(res->status);
            return result;
        }
    } else {
        result.success = false;
        result.status_code = -1;
        result.content = "HTTP 请求底层失败，网络错误码: " + std::to_string(static_cast<int>(res.error()));
        return result;
    }


        }



private:

        std::string api_key_;
        std::string host_;
        std::string path_;

};

#endif // LLM_CLIENT_H