#pragma once

#include <cstdint>
#include <string>

#include "audio_pipeline.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lossless_server.hpp"
#include "sonos.hpp"
#include "wifi_manager.hpp"

namespace cdts {

struct SonosAutomationStatus {
    bool enabled = true;
    bool group_selected = false;
    bool play_commanded = false;
    std::string state = "starting";
    std::string group_ip;
    std::string group_uuid;
    std::string group_name;
    uint32_t discovery_attempts = 0;
    uint32_t play_attempts = 0;
    uint32_t recovery_attempts = 0;
    int64_t last_play_started_us = 0;
};

class SonosAutomation {
public:
    SonosAutomation(AudioPipeline& audio,
                    LosslessServer& lossless,
                    WifiManager& wifi,
                    SonosController& sonos);
    ~SonosAutomation();

    bool start();
    bool select_group(const std::string& ip,
                      const std::string& uuid = {},
                      const std::string& name = {});
    bool play_selected(bool recovering = false);
    bool stop_selected();
    SonosAutomationStatus status() const;

private:
    static void task_entry(void* context);
    void task();
    bool discover_and_select();
    bool load_preference();
    bool save_preference(const SonosDevice& device);
    void set_state(const char* state);

    AudioPipeline& audio_;
    LosslessServer& lossless_;
    WifiManager& wifi_;
    SonosController& sonos_;
    mutable SemaphoreHandle_t mutex_ = nullptr;
    TaskHandle_t task_handle_ = nullptr;
    bool play_in_progress_ = false;
    SonosAutomationStatus status_ = {};
};

}  // namespace cdts
