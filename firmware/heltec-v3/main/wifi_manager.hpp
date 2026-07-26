#pragma once

#include <array>
#include <string>

#include "esp_netif.h"

namespace cdts {

struct WifiNetwork {
    std::string ssid;
    int rssi = -127;
    bool secure = false;
};

class WifiManager {
public:
    static constexpr size_t kMaxScanResults = 20;

    bool start();
    bool save_credentials(const std::string& ssid, const std::string& password);
    size_t scan(std::array<WifiNetwork, kMaxScanResults>& networks);
    bool connected() const;
    bool setup_ap_active() const;
    int rssi() const;
    std::string ip_address() const;
    std::string hostname() const;

private:
    static void event_handler(void* context,
                              esp_event_base_t base,
                              int32_t id,
                              void* data);
    bool load_credentials(std::array<char, 33>& ssid,
                          std::array<char, 65>& password);
    void start_setup_ap();

    esp_netif_t* station_ = nullptr;
    esp_netif_t* access_point_ = nullptr;
    bool connected_ = false;
    bool setup_ap_active_ = false;
    unsigned reconnect_attempts_ = 0;
    std::string hostname_ = "cdtosonos";
};

}  // namespace cdts
