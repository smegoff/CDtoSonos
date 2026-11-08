#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include "audio_pipeline.hpp"

namespace cdts {

struct ListenerCounts {
    uint32_t sonos = 0;
    uint32_t other = 0;
    uint32_t healthy_sonos = 0;
};

class LosslessServer {
public:
    explicit LosslessServer(AudioPipeline& audio);
    bool start(uint16_t port = 8000);
    uint32_t clients() const;
    ListenerCounts listener_counts(const char* sonos_ip) const;
    uint64_t frames_sent() const;

private:
    static constexpr size_t kMaxClients = 3;
    static constexpr uint32_t kPendingClient = UINT32_MAX;
    struct ClientContext {
        LosslessServer* server;
        int socket;
        size_t slot;
        uint32_t peer_ip;
    };
    struct Listener {
        std::atomic<uint32_t> peer = 0;
        std::atomic<uint32_t> last_sent_ms = 0;
        std::atomic<bool> large_skip = false;
    };

    static void listener_entry(void* context);
    static void client_entry(void* context);
    void listener_task();
    void client_task(const ClientContext& client);
    static bool send_all(int socket, const uint8_t* data, size_t size);

    AudioPipeline& audio_;
    uint16_t port_ = 8000;
    // Each slot is free (zero), waiting for a stream request, or streaming
    // to the stored IPv4 address in network byte order.
    std::array<Listener, kMaxClients> listeners_ = {};
    std::atomic<uint64_t> frames_sent_ = 0;
};

}  // namespace cdts

