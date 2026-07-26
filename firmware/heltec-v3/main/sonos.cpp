#include "sonos.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"

namespace cdts {
namespace {
constexpr char kTag[] = "sonos";
constexpr size_t kMaxTopologyBytes = 32768;

struct DiscoveryContext {
    SonosController* controller;
    std::array<SonosDevice, SonosController::kMaxDevices>* devices;
    SemaphoreHandle_t completed;
    size_t count;
};

std::string xml_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 16);
    for (char character : value) {
        switch (character) {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&apos;"; break;
            default: escaped += character; break;
        }
    }
    return escaped;
}

void replace_all(std::string& value,
                 const char* encoded,
                 const char* decoded) {
    size_t offset = 0;
    while ((offset = value.find(encoded, offset)) != std::string::npos) {
        value.replace(offset, std::strlen(encoded), decoded);
        offset += std::strlen(decoded);
    }
}

std::string xml_unescape(std::string value) {
    replace_all(value, "&quot;", "\"");
    replace_all(value, "&apos;", "'");
    replace_all(value, "&lt;", "<");
    replace_all(value, "&gt;", ">");
    replace_all(value, "&amp;", "&");
    return value;
}

std::string attribute_value(const std::string& tag, const char* attribute) {
    const std::string prefix = std::string(attribute) + "=\"";
    const size_t start = tag.find(prefix);
    if (start == std::string::npos) return {};
    const size_t value_start = start + prefix.size();
    const size_t end = tag.find('"', value_start);
    if (end == std::string::npos) return {};
    return xml_unescape(tag.substr(value_start, end - value_start));
}

std::string ip_from_location(const std::string& location) {
    constexpr char prefix[] = "http://";
    const size_t start =
        location.rfind(prefix, 0) == 0 ? sizeof(prefix) - 1 : 0;
    const size_t end = location.find(':', start);
    if (end == std::string::npos) return {};
    return location.substr(start, end - start);
}
}

std::string SonosController::header_value(const char* response,
                                          const char* name) {
    std::string lower_response(response);
    std::transform(lower_response.begin(), lower_response.end(),
                   lower_response.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::string lower_name(name);
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    lower_name += ':';

    const size_t start = lower_response.find(lower_name);
    if (start == std::string::npos) return {};
    size_t value_start = start + lower_name.size();
    while (response[value_start] == ' ') ++value_start;
    const size_t end = lower_response.find("\r\n", value_start);
    return std::string(response + value_start,
                       (end == std::string::npos ? lower_response.size() : end) -
                           value_start);
}

void SonosController::discovery_task(void* argument) {
    auto* context = static_cast<DiscoveryContext*>(argument);
    context->count = context->controller->discover_sync(*context->devices);
    xSemaphoreGive(context->completed);
    vTaskDelete(nullptr);
}

size_t SonosController::discover(
    std::array<SonosDevice, kMaxDevices>& devices) {
    SemaphoreHandle_t completed = xSemaphoreCreateBinary();
    if (completed == nullptr) return 0;
    DiscoveryContext context = {this, &devices, completed, 0};
    const sys_thread_t thread =
        sys_thread_new("sonos_discovery", discovery_task, &context, 16384, 5);
    if (thread == nullptr) {
        vSemaphoreDelete(completed);
        return 0;
    }
    xSemaphoreTake(completed, portMAX_DELAY);
    vSemaphoreDelete(completed);
    return context.count;
}

size_t SonosController::discover_sync(
    std::array<SonosDevice, kMaxDevices>& devices) {
    const int socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) return 0;

    timeval timeout = {.tv_sec = 1, .tv_usec = 500000};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &destination.sin_addr);

    static constexpr char request[] =
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 1\r\n"
        "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
        "\r\n";
    sendto(socket_fd, request, sizeof(request) - 1, 0,
           reinterpret_cast<sockaddr*>(&destination), sizeof(destination));

    std::array<char, 1536> response = {};
    const int64_t discovery_deadline = esp_timer_get_time() + 3000000;
    std::string player_ip;
    std::string player_uuid;
    std::string player_location;
    while (player_ip.empty()) {
        if (esp_timer_get_time() >= discovery_deadline) break;
        sockaddr_in source = {};
        socklen_t source_size = sizeof(source);
        const int received =
            recvfrom(socket_fd, response.data(), response.size() - 1, 0,
                     reinterpret_cast<sockaddr*>(&source), &source_size);
        if (received <= 0) break;
        response[received] = '\0';

        char address[16] = {};
        inet_ntop(AF_INET, &source.sin_addr, address, sizeof(address));
        player_ip = address;
        player_uuid = header_value(response.data(), "usn");
        player_location = header_value(response.data(), "location");
        const size_t separator = player_uuid.find("::");
        if (separator != std::string::npos) player_uuid.resize(separator);
        if (player_uuid.rfind("uuid:", 0) == 0) player_uuid.erase(0, 5);
    }
    close(socket_fd);
    if (player_ip.empty()) return 0;

    size_t group_count = 0;
    if (resolve_groups(player_ip, devices, group_count)) {
        ESP_LOGI(kTag, "Resolved %u Sonos room/group coordinators",
                 static_cast<unsigned>(group_count));
        return group_count;
    }

    ESP_LOGW(kTag, "Topology lookup failed; returning discovered player");
    devices[0] = {player_ip, player_uuid, player_location,
                  "Sonos at " + player_ip};
    return 1;
}

bool SonosController::soap_request(const std::string& player_ip,
                                   const char* control_path,
                                   const char* service,
                                   const char* action,
                                   const std::string& body,
                                   std::string* response) {
    const int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd < 0) return false;

    timeval timeout = {.tv_sec = 4, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    sockaddr_in destination = {};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(1400);
    if (inet_pton(AF_INET, player_ip.c_str(), &destination.sin_addr) != 1 ||
        connect(socket_fd, reinterpret_cast<sockaddr*>(&destination),
                sizeof(destination)) != 0) {
        close(socket_fd);
        return false;
    }

    const std::string request =
        std::string("POST ") + control_path +
        " HTTP/1.1\r\nHost: " + player_ip +
        ":1400\r\nContent-Type: text/xml; charset=\"utf-8\"\r\n"
        "SOAPACTION: \"" +
        service + "#" + action +
        "\"\r\nConnection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    size_t sent = 0;
    while (sent < request.size()) {
        const int written =
            send(socket_fd, request.data() + sent, request.size() - sent, 0);
        if (written <= 0) {
            close(socket_fd);
            return false;
        }
        sent += written;
    }

    std::string received_response;
    received_response.reserve(response == nullptr ? 256 : 8192);
    std::array<char, 1024> chunk = {};
    bool overflow = false;
    const int64_t response_deadline = esp_timer_get_time() + 6000000;
    while (true) {
        const int received = recv(socket_fd, chunk.data(), chunk.size(), 0);
        if (received <= 0) break;
        if (received_response.size() + received > kMaxTopologyBytes) {
            overflow = true;
            break;
        }
        received_response.append(chunk.data(), received);
        if (esp_timer_get_time() >= response_deadline) break;
    }
    close(socket_fd);

    const size_t status_offset = received_response.find(' ');
    if (overflow || status_offset == std::string::npos ||
        received_response.compare(status_offset + 1, 3, "200") != 0) {
        return false;
    }
    if (response != nullptr) *response = std::move(received_response);
    return true;
}

bool SonosController::resolve_groups(
    const std::string& topology_player_ip,
    std::array<SonosDevice, kMaxDevices>& groups,
    size_t& group_count) {
    group_count = 0;
    if (topology_player_ip.empty()) return false;

    const std::string body =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:GetZoneGroupState "
        "xmlns:u=\"urn:schemas-upnp-org:service:ZoneGroupTopology:1\"/>"
        "</s:Body></s:Envelope>";
    std::string response;
    if (!soap_request(topology_player_ip, "/ZoneGroupTopology/Control",
                      "urn:schemas-upnp-org:service:ZoneGroupTopology:1",
                      "GetZoneGroupState", body, &response)) {
        return false;
    }

    const size_t state_start = response.find("<ZoneGroupState>");
    const size_t state_end = response.find("</ZoneGroupState>", state_start);
    if (state_start == std::string::npos || state_end == std::string::npos) {
        return false;
    }
    const std::string topology = xml_unescape(
        response.substr(state_start, state_end - state_start));

    size_t position = 0;
    while (group_count < groups.size()) {
        const size_t group_start = topology.find("<ZoneGroup ", position);
        if (group_start == std::string::npos) break;
        const size_t opening_end = topology.find('>', group_start);
        const size_t group_end = topology.find("</ZoneGroup>", opening_end);
        if (opening_end == std::string::npos ||
            group_end == std::string::npos) {
            return false;
        }

        const std::string opening =
            topology.substr(group_start, opening_end - group_start + 1);
        const std::string coordinator =
            attribute_value(opening, "Coordinator");
        std::string coordinator_ip;

        std::string group_name;
        std::string coordinator_location;
        size_t member_position = opening_end + 1;
        while (member_position < group_end) {
            const size_t member_start =
                topology.find("<ZoneGroupMember ", member_position);
            if (member_start == std::string::npos ||
                member_start >= group_end) {
                break;
            }
            const size_t member_end = topology.find('>', member_start);
            if (member_end == std::string::npos || member_end >= group_end) {
                return false;
            }
            const std::string member =
                topology.substr(member_start, member_end - member_start + 1);
            const std::string room_name = attribute_value(member, "ZoneName");
            const std::string member_uuid = attribute_value(member, "UUID");
            const std::string location = attribute_value(member, "Location");
            if (!room_name.empty() &&
                group_name.find(room_name) == std::string::npos) {
                if (!group_name.empty()) group_name += " + ";
                group_name += room_name;
            }
            if (member_uuid == coordinator) {
                coordinator_location = location;
                if (coordinator_ip.empty()) {
                    coordinator_ip = ip_from_location(location);
                }
            }
            member_position = member_end + 1;
        }

        if (!coordinator.empty() && !coordinator_ip.empty()) {
            if (group_name.empty()) group_name = "Sonos room";
            groups[group_count++] = {coordinator_ip, coordinator,
                                     coordinator_location, group_name};
        }
        position = group_end + std::strlen("</ZoneGroup>");
    }
    return group_count > 0;
}

bool SonosController::play_stream(const std::string& player_ip,
                                  const std::string& stream_url) {
    // A Sonos coordinator can retain PLAYING with the same URI after the
    // source device reboots, yet have no HTTP connection. Force a clean
    // transport transition so Play always causes a fresh stream request.
    stop(player_ip);

    const std::string envelope_start =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>";
    const std::string envelope_end = "</s:Body></s:Envelope>";

    const std::string set_uri =
        envelope_start +
        "<u:SetAVTransportURI "
        "xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID><CurrentURI>" +
        xml_escape(stream_url) +
        "</CurrentURI><CurrentURIMetaData></CurrentURIMetaData>"
        "</u:SetAVTransportURI>" +
        envelope_end;
    if (!soap_request(player_ip, "/MediaRenderer/AVTransport/Control",
                      "urn:schemas-upnp-org:service:AVTransport:1",
                      "SetAVTransportURI", set_uri)) {
        return false;
    }

    const std::string play =
        envelope_start +
        "<u:Play xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID><Speed>1</Speed></u:Play>" +
        envelope_end;
    return soap_request(player_ip, "/MediaRenderer/AVTransport/Control",
                        "urn:schemas-upnp-org:service:AVTransport:1", "Play",
                        play);
}

bool SonosController::stop(const std::string& player_ip) {
    const std::string body =
        "<?xml version=\"1.0\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body>"
        "<u:Stop xmlns:u=\"urn:schemas-upnp-org:service:AVTransport:1\">"
        "<InstanceID>0</InstanceID></u:Stop>"
        "</s:Body></s:Envelope>";
    return soap_request(player_ip, "/MediaRenderer/AVTransport/Control",
                        "urn:schemas-upnp-org:service:AVTransport:1", "Stop",
                        body);
}

}  // namespace cdts
