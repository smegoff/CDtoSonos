#include "wifi_manager.hpp"

#include <algorithm>
#include <cstring>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "mdns.h"
#include "nvs.h"

namespace cdts {
namespace {
constexpr char kTag[] = "wifi";
constexpr char kNamespace[] = "network";
}

bool WifiManager::load_credentials(std::array<char, 33>& ssid,
                                   std::array<char, 65>& password) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return false;
    }
    size_t ssid_size = ssid.size();
    size_t password_size = password.size();
    const bool ok =
        nvs_get_str(handle, "ssid", ssid.data(), &ssid_size) == ESP_OK &&
        nvs_get_str(handle, "password", password.data(), &password_size) ==
            ESP_OK &&
        ssid[0] != '\0';
    nvs_close(handle);
    return ok;
}

bool WifiManager::save_credentials(const std::string& ssid,
                                   const std::string& password) {
    if (ssid.empty() || ssid.size() > 32 || password.size() > 64) {
        return false;
    }
    nvs_handle_t handle = 0;
    if (nvs_open(kNamespace, NVS_READWRITE, &handle) != ESP_OK) {
        return false;
    }
    const bool ok =
        nvs_set_str(handle, "ssid", ssid.c_str()) == ESP_OK &&
        nvs_set_str(handle, "password", password.c_str()) == ESP_OK &&
        nvs_commit(handle) == ESP_OK;
    nvs_close(handle);
    return ok;
}

size_t WifiManager::scan(
    std::array<WifiNetwork, kMaxScanResults>& networks) {
    if (esp_wifi_scan_start(nullptr, true) != ESP_OK) {
        return 0;
    }

    uint16_t available = 0;
    if (esp_wifi_scan_get_ap_num(&available) != ESP_OK || available == 0) {
        return 0;
    }

    std::array<wifi_ap_record_t, kMaxScanResults> records = {};
    uint16_t count = std::min<uint16_t>(available, records.size());
    if (esp_wifi_scan_get_ap_records(&count, records.data()) != ESP_OK) {
        return 0;
    }

    size_t output_count = 0;
    for (uint16_t index = 0;
         index < count && output_count < networks.size(); ++index) {
        const char* ssid =
            reinterpret_cast<const char*>(records[index].ssid);
        if (ssid[0] == '\0') continue;

        const auto duplicate = std::find_if(
            networks.begin(), networks.begin() + output_count,
            [&](const WifiNetwork& network) { return network.ssid == ssid; });
        if (duplicate != networks.begin() + output_count) {
            if (records[index].rssi > duplicate->rssi) {
                duplicate->rssi = records[index].rssi;
            }
            continue;
        }

        networks[output_count++] = {
            ssid, records[index].rssi,
            records[index].authmode != WIFI_AUTH_OPEN};
    }
    std::sort(networks.begin(), networks.begin() + output_count,
              [](const WifiNetwork& left, const WifiNetwork& right) {
                  return left.rssi > right.rssi;
              });
    return output_count;
}

void WifiManager::event_handler(void* context,
                                esp_event_base_t base,
                                int32_t id,
                                void* data) {
    auto* manager = static_cast<WifiManager*>(context);
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        manager->connected_ = false;
        if (manager->reconnect_attempts_++ < 10) {
            esp_wifi_connect();
        } else if (!manager->setup_ap_active_) {
            manager->start_setup_ap();
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        manager->connected_ = true;
        manager->reconnect_attempts_ = 0;
        const auto* event = static_cast<ip_event_got_ip_t*>(data);
        ESP_LOGI(kTag, "Connected, IP " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void WifiManager::start_setup_ap() {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    char ap_name[33] = {};
    std::snprintf(ap_name, sizeof(ap_name), "CDtoSonos-%02X%02X",
                  mac[4], mac[5]);

    wifi_config_t ap = {};
    std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), ap_name,
                 sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = std::strlen(ap_name);
    ap.ap.channel = 6;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    setup_ap_active_ = true;
    ESP_LOGW(kTag, "Setup AP active: %s (open, commissioning only)", ap_name);
}

bool WifiManager::start() {
    station_ = esp_netif_create_default_wifi_sta();
    access_point_ = esp_netif_create_default_wifi_ap();
    if (station_ == nullptr || access_point_ == nullptr) {
        return false;
    }

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, event_handler, this, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, event_handler, this, nullptr));

    std::array<char, 33> ssid = {};
    std::array<char, 65> password = {};
    const bool provisioned = load_credentials(ssid, password);

    ESP_ERROR_CHECK(esp_wifi_set_mode(provisioned ? WIFI_MODE_STA
                                                  : WIFI_MODE_APSTA));
    if (provisioned) {
        wifi_config_t station = {};
        std::strncpy(reinterpret_cast<char*>(station.sta.ssid), ssid.data(),
                     sizeof(station.sta.ssid) - 1);
        std::strncpy(reinterpret_cast<char*>(station.sta.password),
                     password.data(), sizeof(station.sta.password) - 1);
        station.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        station.sta.pmf_cfg.capable = true;
        station.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &station));
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    // Continuous lossless audio is latency-sensitive. Modem power saving can
    // introduce bursts long enough to drain the Sonos input buffer.
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    if (!provisioned) {
        start_setup_ap();
    }

    ESP_ERROR_CHECK(mdns_init());
    ESP_ERROR_CHECK(mdns_hostname_set(hostname_.c_str()));
    ESP_ERROR_CHECK(mdns_instance_name_set("CD to Sonos"));
    ESP_ERROR_CHECK(mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0));
    ESP_ERROR_CHECK(
        mdns_service_add("Lossless CD stream", "_http", "_tcp", 8000,
                         nullptr, 0));
    return true;
}

bool WifiManager::connected() const {
    return connected_;
}

bool WifiManager::setup_ap_active() const {
    return setup_ap_active_;
}

int WifiManager::rssi() const {
    if (!connected_) return -127;
    wifi_ap_record_t access_point = {};
    return esp_wifi_sta_get_ap_info(&access_point) == ESP_OK
               ? access_point.rssi
               : -127;
}

std::string WifiManager::ip_address() const {
    esp_netif_ip_info_t info = {};
    esp_netif_t* interface = connected_ ? station_ : access_point_;
    if (interface == nullptr ||
        esp_netif_get_ip_info(interface, &info) != ESP_OK) {
        return "0.0.0.0";
    }
    char address[16] = {};
    esp_ip4addr_ntoa(&info.ip, address, sizeof(address));
    return address;
}

std::string WifiManager::hostname() const {
    return hostname_;
}

}  // namespace cdts
