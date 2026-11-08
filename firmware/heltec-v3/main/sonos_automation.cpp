#include "sonos_automation.hpp"

#include <array>

#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"

namespace cdts {
namespace {
constexpr char kTag[] = "sonos_auto";
constexpr char kNvsNamespace[] = "sonos_auto";
constexpr int64_t kAudioQualificationUs = 1500000;
constexpr int64_t kSilenceTimeoutUs = 90000000;
constexpr int64_t kDiscoveryRetryUs = 15000000;
constexpr int64_t kPlaybackRetryUs = 5000000;
constexpr int64_t kRecoveryCooldownUs = 30000000;
constexpr uint32_t kFreshCaptureMs = 250;

std::string read_nvs_string(nvs_handle_t handle, const char* key) {
    size_t length = 0;
    if (nvs_get_str(handle, key, nullptr, &length) != ESP_OK ||
        length == 0 || length > 192) {
        return {};
    }
    std::string value(length, '\0');
    if (nvs_get_str(handle, key, value.data(), &length) != ESP_OK) return {};
    if (!value.empty() && value.back() == '\0') value.pop_back();
    return value;
}
}

SonosAutomation::SonosAutomation(AudioPipeline& audio,
                                 LosslessServer& lossless,
                                 WifiManager& wifi,
                                 SonosController& sonos)
    : audio_(audio), lossless_(lossless), wifi_(wifi), sonos_(sonos) {
    mutex_ = xSemaphoreCreateMutex();
}

SonosAutomation::~SonosAutomation() {
    if (task_handle_ != nullptr) vTaskDelete(task_handle_);
    if (mutex_ != nullptr) vSemaphoreDelete(mutex_);
}

bool SonosAutomation::start() {
    if (mutex_ == nullptr) return false;
    load_preference();
    return xTaskCreate(task_entry, "sonos_auto", 8192, this, 4,
                       &task_handle_) == pdPASS;
}

void SonosAutomation::task_entry(void* context) {
    static_cast<SonosAutomation*>(context)->task();
}

void SonosAutomation::set_state(const char* state) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.state = state;
    xSemaphoreGive(mutex_);
}

SonosAutomationStatus SonosAutomation::status() const {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    const SonosAutomationStatus copy = status_;
    xSemaphoreGive(mutex_);
    return copy;
}

bool SonosAutomation::load_preference() {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READONLY, &handle) != ESP_OK) return false;
    const std::string ip = read_nvs_string(handle, "ip");
    const std::string uuid = read_nvs_string(handle, "uuid");
    const std::string name = read_nvs_string(handle, "name");
    nvs_close(handle);
    if (ip.empty() && uuid.empty()) return false;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.group_ip = ip;
    status_.group_uuid = uuid;
    status_.group_name = name;
    xSemaphoreGive(mutex_);
    ESP_LOGI(kTag, "Loaded preferred Sonos group %s (%s)",
             name.c_str(), ip.c_str());
    return true;
}

bool SonosAutomation::save_preference(const SonosDevice& device) {
    nvs_handle_t handle = 0;
    if (nvs_open(kNvsNamespace, NVS_READWRITE, &handle) != ESP_OK) return false;
    esp_err_t result = nvs_set_str(handle, "ip", device.ip.c_str());
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "uuid", device.uuid.c_str());
    }
    if (result == ESP_OK) {
        result = nvs_set_str(handle, "name", device.name.c_str());
    }
    if (result == ESP_OK) result = nvs_commit(handle);
    nvs_close(handle);
    return result == ESP_OK;
}

bool SonosAutomation::select_group(const std::string& ip,
                                   const std::string& uuid,
                                   const std::string& name) {
    if (ip.empty()) return false;
    SonosDevice device;
    device.ip = ip;
    device.uuid = uuid;
    device.name = name.empty() ? ip : name;

    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.group_ip = device.ip;
    status_.group_uuid = device.uuid;
    status_.group_name = device.name;
    status_.group_selected = true;
    xSemaphoreGive(mutex_);
    const bool saved = save_preference(device);
    ESP_LOGI(kTag, "Preferred Sonos group set to %s (%s)",
             device.name.c_str(), device.ip.c_str());
    return saved;
}

bool SonosAutomation::discover_and_select() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    ++status_.discovery_attempts;
    status_.state = "discovering";
    const std::string preferred_uuid = status_.group_uuid;
    const std::string preferred_ip = status_.group_ip;
    xSemaphoreGive(mutex_);

    std::array<SonosDevice, SonosController::kMaxDevices> devices = {};
    const size_t count = sonos_.discover(devices);
    if (count == 0) {
        set_state("discovery retry");
        ESP_LOGW(kTag, "No Sonos coordinator found; will retry");
        return false;
    }

    size_t selected = 0;
    bool matched = false;
    for (size_t index = 0; index < count; ++index) {
        if ((!preferred_uuid.empty() &&
             devices[index].uuid == preferred_uuid) ||
            (!preferred_ip.empty() && devices[index].ip == preferred_ip)) {
            selected = index;
            matched = true;
            break;
        }
    }
    if (!matched && count > 1) {
        ESP_LOGW(kTag,
                 "No saved group matched; selecting first of %u coordinators",
                 static_cast<unsigned>(count));
    }

    const SonosDevice& device = devices[selected];
    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.group_ip = device.ip;
    status_.group_uuid = device.uuid;
    status_.group_name = device.name;
    status_.group_selected = true;
    status_.state = "ready";
    xSemaphoreGive(mutex_);
    save_preference(device);
    ESP_LOGI(kTag, "Selected Sonos coordinator %s (%s)",
             device.name.c_str(), device.ip.c_str());
    return true;
}

bool SonosAutomation::play_selected(bool recovering) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    if (play_in_progress_ || !status_.group_selected || status_.group_ip.empty() ||
        !wifi_.connected() || (recovering && !status_.play_commanded)) {
        xSemaphoreGive(mutex_);
        return false;
    }
    const std::string group_ip = status_.group_ip;
    play_in_progress_ = true;
    ++status_.play_attempts;
    if (recovering) ++status_.recovery_attempts;
    status_.last_play_started_us = esp_timer_get_time();
    status_.state = recovering ? "restarting stream" : "connecting";
    xSemaphoreGive(mutex_);

    const std::string stream_url =
        "http://" + wifi_.ip_address() + ":8000/cd.flac";
    const bool ok = sonos_.play_stream(group_ip, stream_url);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    play_in_progress_ = false;
    if (status_.group_ip == group_ip) {
        status_.play_commanded = ok;
        status_.state = ok ? "streaming" : "play retry";
    }
    xSemaphoreGive(mutex_);
    ESP_LOGI(kTag, "Sonos %s %s", recovering ? "restart" : "play",
             ok ? "accepted" : "failed");
    return ok;
}

bool SonosAutomation::stop_selected() {
    const SonosAutomationStatus current = status();
    if (!current.group_selected || current.group_ip.empty()) return false;
    const bool ok = sonos_.stop(current.group_ip);
    xSemaphoreTake(mutex_, portMAX_DELAY);
    status_.play_commanded = false;
    status_.state = ok ? "ready" : "stop failed";
    xSemaphoreGive(mutex_);
    return ok;
}

void SonosAutomation::task() {
    int64_t audio_since = 0;
    int64_t silence_since = 0;
    int64_t last_discovery = -kDiscoveryRetryUs;
    int64_t last_recovery = -kRecoveryCooldownUs;

    while (true) {
        const int64_t now = esp_timer_get_time();
        if (!wifi_.connected()) {
            set_state("waiting for Wi-Fi");
            audio_since = 0;
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        SonosAutomationStatus current = status();
        if (!current.group_selected &&
            now - last_discovery >= kDiscoveryRetryUs) {
            last_discovery = now;
            discover_and_select();
            current = status();
        }

        const AudioMetrics metrics = audio_.metrics();
        if (metrics.clock_locked && metrics.signal_present &&
            metrics.capture_age_ms < kFreshCaptureMs) {
            silence_since = 0;
            if (audio_since == 0) audio_since = now;

            const ListenerCounts listeners =
                lossless_.listener_counts(current.group_ip.c_str());
            const bool client_missing = current.play_commanded && listeners.sonos == 0;
            const bool stream_unhealthy = current.play_commanded &&
                listeners.sonos != 0 && listeners.healthy_sonos == 0;
            const bool needs_play = !current.play_commanded || client_missing;
            const bool starting = current.state == "connecting" ||
                current.state == "restarting stream";
            if (current.play_commanded && listeners.healthy_sonos != 0 &&
                !starting && current.state != "streaming") {
                set_state("streaming");
            }
            if (current.group_selected && !starting && (needs_play || stream_unhealthy) &&
                now - audio_since >= kAudioQualificationUs &&
                (current.last_play_started_us == 0 ||
                 now - current.last_play_started_us >= kPlaybackRetryUs) &&
                now - last_recovery >= kRecoveryCooldownUs) {
                if (stream_unhealthy) last_recovery = now;
                play_selected(stream_unhealthy);
            }
        } else {
            audio_since = 0;
            if (current.play_commanded) {
                if (silence_since == 0) silence_since = now;
                if (now - silence_since >= kSilenceTimeoutUs) {
                    set_state("stopping after silence");
                    stop_selected();
                    silence_since = 0;
                } else {
                    set_state("track-gap hold");
                }
            } else if (current.group_selected) {
                set_state("ready");
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

}  // namespace cdts
