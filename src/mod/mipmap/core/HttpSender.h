#pragma once
#include <string>
#include <functional>
#include "../httplib.h"

namespace mipmap {

class HttpSender {
public:
    struct Config {
        std::string host = "0.0.0.0";
        int         port = 8080;
    };

    explicit HttpSender(Config config) : mConfig(config) {}

    // Fire-and-forget, jalan di thread terpisah
    void sendChunkData(std::string jsonBody) {
        std::thread([this, body = std::move(jsonBody)]() {
            try {
                httplib::Client cli(mConfig.host, mConfig.port);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(3);
                cli.Post("/api/chunks-data", body, "application/json");
            } catch (...) {}
        }).detach();
    }

    void sendPlayerData(std::string jsonBody) {
        std::thread([this, body = std::move(jsonBody)]() {
            try {
                httplib::Client cli(mConfig.host, mConfig.port);
                cli.set_connection_timeout(3);
                cli.set_read_timeout(3);
                cli.Post("/api/players-data", body, "application/json");
            } catch (...) {}
        }).detach();
    }

private:
    Config mConfig;
};

} // namespace mipmap