#pragma once

#include <atomic>
#include <cstdint>

#include "audio_pipeline.hpp"

namespace cdts {

class LosslessServer {
public:
    explicit LosslessServer(AudioPipeline& audio);
    bool start(uint16_t port = 8000);
    uint32_t clients() const;
    uint64_t frames_sent() const;

private:
    struct ClientContext {
        LosslessServer* server;
        int socket;
    };

    static void listener_entry(void* context);
    static void client_entry(void* context);
    void listener_task();
    void client_task(int socket);
    static bool send_all(int socket, const uint8_t* data, size_t size);

    AudioPipeline& audio_;
    uint16_t port_ = 8000;
    std::atomic<uint32_t> clients_ = 0;
    std::atomic<uint64_t> frames_sent_ = 0;
};

}  // namespace cdts

