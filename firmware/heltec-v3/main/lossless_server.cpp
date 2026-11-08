#include "lossless_server.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>

#include "board.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "flac_encoder.hpp"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace cdts {
namespace {
constexpr char kTag[] = "flac_http";
constexpr uint32_t kSendStallMs = 5000;
constexpr uint32_t kSkipWindowMs = 5000;
constexpr uint32_t kLargeSkipBlocks =
    (board::kSampleRate + board::kFlacBlockSamples - 1) / board::kFlacBlockSamples;

uint32_t now_ms() {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

// These buffers total more than 8 KiB, so keeping them as client_task locals
// overflows the FreeRTOS task stack as soon as a listener connects. Allocate
// them from the heap instead; each active stream client owns one set.
struct ClientBuffers {
    std::array<uint8_t, FlacEncoder::kStreamHeaderBytes> header = {};
    std::array<uint8_t, FlacEncoder::kMaxFrameBytes> encoded = {};
    std::array<int16_t, AudioPipeline::kSamplesPerBlock> pcm = {};
};
}

LosslessServer::LosslessServer(AudioPipeline& audio) : audio_(audio) {}

bool LosslessServer::start(uint16_t port) {
    port_ = port;
    return xTaskCreatePinnedToCore(listener_entry, "flac_listener", 4096, this,
                                   8, nullptr, 0) == pdPASS;
}

uint32_t LosslessServer::clients() const {
    return listener_counts("").other;
}

ListenerCounts LosslessServer::listener_counts(const char* sonos_ip) const {
    in_addr selected = {};
    const bool selected_valid = inet_pton(AF_INET, sonos_ip, &selected) == 1;
    ListenerCounts counts;
    for (const auto& listener : listeners_) {
        const uint32_t peer = listener.peer.load();
        if (peer == 0 || peer == kPendingClient) continue;
        if (selected_valid && peer == selected.s_addr) {
            ++counts.sonos;
            const uint32_t last_sent = listener.last_sent_ms.load();
            if (!listener.large_skip.load() && now_ms() - last_sent < kSendStallMs) {
                ++counts.healthy_sonos;
            }
        } else {
            ++counts.other;
        }
    }
    return counts;
}

uint64_t LosslessServer::frames_sent() const {
    return frames_sent_.load();
}

void LosslessServer::listener_entry(void* context) {
    static_cast<LosslessServer*>(context)->listener_task();
}

void LosslessServer::client_entry(void* context) {
    auto* client = static_cast<ClientContext*>(context);
    client->server->client_task(*client);
    client->server->listeners_[client->slot].peer.store(0);
    delete client;
    vTaskDelete(nullptr);
}

void LosslessServer::listener_task() {
    const int listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listen_socket < 0) {
        ESP_LOGE(kTag, "Unable to create listening socket: errno %d", errno);
        vTaskDelete(nullptr);
        return;
    }

    int reuse = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(listen_socket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0 ||
        listen(listen_socket, kMaxClients) != 0) {
        ESP_LOGE(kTag, "Unable to bind/listen on %u: errno %d", port_, errno);
        close(listen_socket);
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(kTag, "Lossless FLAC stream listening on port %u", port_);
    while (true) {
        sockaddr_in source = {};
        socklen_t source_length = sizeof(source);
        const int client_socket =
            accept(listen_socket, reinterpret_cast<sockaddr*>(&source),
                   &source_length);
        if (client_socket < 0) {
            ESP_LOGW(kTag, "Accept failed: errno %d", errno);
            continue;
        }
        size_t slot = 0;
        while (slot < listeners_.size() && listeners_[slot].peer.load() != 0) ++slot;
        if (slot == listeners_.size()) {
            static constexpr char busy[] =
                "HTTP/1.1 503 Busy\r\nConnection: close\r\n\r\n";
            send(client_socket, busy, sizeof(busy) - 1, 0);
            close(client_socket);
            continue;
        }

        listeners_[slot].peer.store(kPendingClient);
        auto* context = new ClientContext{
            this, client_socket, slot, source.sin_addr.s_addr};
        if (xTaskCreatePinnedToCore(client_entry, "flac_client", 8192, context,
                                    7, nullptr, 0) != pdPASS) {
            delete context;
            close(client_socket);
            listeners_[slot].peer.store(0);
        }
    }
}

bool LosslessServer::send_all(int socket_fd,
                              const uint8_t* data,
                              size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const int result =
            send(socket_fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

void LosslessServer::client_task(const ClientContext& client) {
    const int socket_fd = client.socket;
    timeval timeout = {.tv_sec = 8, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::array<char, 768> request = {};
    const int received = recv(socket_fd, request.data(), request.size() - 1, 0);
    if (received <= 0 ||
        (std::strstr(request.data(), "GET /cd.flac ") == nullptr &&
         std::strstr(request.data(), "GET /stream.flac ") == nullptr)) {
        static constexpr char not_found[] =
            "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
        send(socket_fd, not_found, sizeof(not_found) - 1, 0);
        close(socket_fd);
        return;
    }

    static constexpr char response[] =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: audio/flac\r\n"
        "Cache-Control: no-store, no-cache\r\n"
        "Connection: close\r\n"
        "icy-name: CD to Sonos Lossless\r\n"
        "icy-br: 1412\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n";
    if (!send_all(socket_fd,
                  reinterpret_cast<const uint8_t*>(response),
                  sizeof(response) - 1)) {
        close(socket_fd);
        return;
    }

    FlacEncoder encoder(board::kSampleRate, board::kFlacBlockSamples);
    auto buffers = std::make_unique<ClientBuffers>();

    const size_t header_size =
        encoder.stream_header(buffers->header.data(), buffers->header.size());
    bool connected =
        send_all(socket_fd, buffers->header.data(), header_size);
    Listener& listener = listeners_[client.slot];
    if (connected) {
        listener.last_sent_ms.store(now_ms());
        listener.large_skip.store(false);
        listener.peer.store(client.peer_ip);
    }
    uint64_t sequence = 0;
    uint32_t client_frame = 0;
    uint32_t skip_window_start = 0;
    uint64_t skipped_in_window = 0;

    while (connected) {
        uint64_t next_sequence = 0;
        if (!audio_.copy_after(sequence, buffers->pcm.data(), buffers->pcm.size(),
                               &next_sequence, 1000)) {
            continue;
        }
        if (sequence != 0 && next_sequence > sequence + 1) {
            const uint32_t now = now_ms();
            if (skipped_in_window == 0 || now - skip_window_start >= kSkipWindowMs) {
                skip_window_start = now;
                skipped_in_window = 0;
            }
            skipped_in_window += next_sequence - sequence - 1;
            if (skipped_in_window >= kLargeSkipBlocks) {
                listener.large_skip.store(true);
            }
        }
        sequence = next_sequence;
        const size_t frame_size =
            encoder.encode_stereo_16(
                buffers->pcm.data(), board::kFlacBlockSamples, client_frame++,
                buffers->encoded.data(), buffers->encoded.size());
        connected =
            frame_size > 0 &&
            send_all(socket_fd, buffers->encoded.data(), frame_size);
        if (connected) {
            listener.last_sent_ms.store(now_ms());
            frames_sent_.fetch_add(1);
        }
    }

    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
}

}  // namespace cdts
