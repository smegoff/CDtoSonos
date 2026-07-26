#pragma once

#include <array>
#include <cstddef>
#include <string>

namespace cdts {

struct SonosDevice {
    std::string ip;
    std::string uuid;
    std::string location;
    std::string name;
};

class SonosController {
public:
    static constexpr size_t kMaxDevices = 8;
    size_t discover(std::array<SonosDevice, kMaxDevices>& devices);
    bool play_stream(const std::string& player_ip,
                     const std::string& stream_url);
    bool stop(const std::string& player_ip);

private:
    static void discovery_task(void* argument);
    size_t discover_sync(std::array<SonosDevice, kMaxDevices>& devices);
    static std::string header_value(const char* response, const char* name);
    static bool soap_request(const std::string& player_ip,
                             const char* control_path,
                             const char* service,
                             const char* action,
                             const std::string& body,
                             std::string* response = nullptr);
    static bool resolve_groups(
        const std::string& topology_player_ip,
        std::array<SonosDevice, kMaxDevices>& groups,
        size_t& group_count);
};

}  // namespace cdts
