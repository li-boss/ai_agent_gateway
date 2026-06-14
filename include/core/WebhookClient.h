#ifndef WEBHOOK_CLIENT_H
#define WEBHOOK_CLIENT_H

#include <string>
#include "Logger.h"
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../third_party/httplib.h"
#include "../third_party/json.hpp"

using json = nlohmann::json;

class WebhookClient {
public:
    WebhookClient(const std::string& controlHost, int controlPort)
        : controlHost_(controlHost), controlPort_(controlPort) {}

    void notifyResult(int taskId, const std::string& result) {
        try {
            // Use direct host and port constructor to prevent scheme parsing issues
            httplib::Client cli(controlHost_, controlPort_);
            cli.set_connection_timeout(3);

            json payload = {
                {"taskId", taskId},
                {"result", result},
                {"status", "Done"}
            };

            std::string path = "/api/webhook";
            auto res = cli.Post(path, payload.dump(), "application/json");

            if (res) {
                if (res->status == 200) {
                    LOG_INFO << "Webhook callback success for task " << taskId;
                } else {
                    LOG_WARN << "Webhook callback returned status code: " << res->status << " for task " << taskId;
                }
            } else {
                LOG_WARN << "Webhook post failed for task " << taskId << ", error code: " << static_cast<int>(res.error());
            }
        } catch (const std::exception& e) {
            LOG_WARN << "Webhook callback failed with exception: " << e.what() << " for task " << taskId;
        }
    }

private:
    std::string controlHost_;
    int controlPort_;
};

#endif // WEBHOOK_CLIENT_H
