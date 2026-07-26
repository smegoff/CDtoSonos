#include "lossless_server.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <memory>

#include "board.hpp"
#include "esp_log.h"
#include "flac_encoder.hpp"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

namespace cdts {
namespace {
constexpr char kTag[] = "flac_http";
constexpr uint32_t kMaxClients = 3;

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
    return clients_.load();
}

uint64_t LosslessServer::frames_sent() const {
    return frames_sent_.load();
}

void LosslessServer::listener_entry(void* context) {
    static_cast<LosslessServer*>(context)->listener_task();
}

void LosslessServer::client_entry(void* context) {
    auto* client = static_cast<ClientContext*>(context);
    client->server->client_task(client->socket);
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
        sockaddr_storage source = {};
        socklen_t source_length = sizeof(source);
        const int client_socket =
            accept(listen_socket, reinterpret_cast<sockaddr*>(&source),
                   &source_length);
        if (client_socket < 0) {
            ESP_LOGW(kTag, "Accept failed: errno %d", errno);
            continue;
        }
        if (clients_.load() >= kMaxClients) {
            static constexpr char busy[] =
                "HTTP/1.1 503 Busy\r\nConnection: close\r\n\r\n";
            send(client_socket, busy, sizeof(busy) - 1, 0);
            close(client_socket);
            continue;
        }

        auto* context = new ClientContext{this, client_socket};
        if (xTaskCreatePinnedToCore(client_entry, "flac_client", 8192, context,
                                    7, nullptr, 0) != pdPASS) {
            delete context;
            close(client_socket);
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

void LosslessServer::client_task(int socket_fd) {
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

    clients_.fetch_add(1);
    FlacEncoder encoder(board::kSampleRate, board::kFlacBlockSamples);
    auto buffers = std::make_unique<ClientBuffers>();

    const size_t header_size =
        encoder.stream_header(buffers->header.data(), buffers->header.size());
    bool connected =
        send_all(socket_fd, buffers->header.data(), header_size);
    uint64_t sequence = 0;
    uint32_t client_frame = 0;

    while (connected) {
        uint64_t next_sequence = 0;
        if (!audio_.copy_after(sequence, buffers->pcm.data(), buffers->pcm.size(),
                               &next_sequence, 1000)) {
            continue;
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
            frames_sent_.fetch_add(1);
        }
    }

    clients_.fetch_sub(1);
    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
}

}  // namespace cdts
