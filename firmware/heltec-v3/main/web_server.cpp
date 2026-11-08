#include "web_server.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "board.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "web_assets.hpp"

namespace cdts {
namespace {
constexpr char kTag[] = "web";

struct CpuUsage {
    float total = 0.0F;
    float core0 = 0.0F;
    float core1 = 0.0F;
};

CpuUsage current_cpu_usage() {
    static int64_t previous_time_us = 0;
    static uint32_t previous_idle0 = 0;
    static uint32_t previous_idle1 = 0;

    const int64_t now_us = esp_timer_get_time();
    const uint32_t idle0 =
        static_cast<uint32_t>(ulTaskGetIdleRunTimeCounterForCore(0));
    const uint32_t idle1 =
        static_cast<uint32_t>(ulTaskGetIdleRunTimeCounterForCore(1));

    if (previous_time_us == 0 || now_us <= previous_time_us) {
        previous_time_us = now_us;
        previous_idle0 = idle0;
        previous_idle1 = idle1;
        return {};
    }

    const float interval_us = static_cast<float>(now_us - previous_time_us);
    const uint32_t idle0_delta = idle0 - previous_idle0;
    const uint32_t idle1_delta = idle1 - previous_idle1;
    CpuUsage result;
    result.core0 = std::clamp(
        100.0F * (1.0F - (static_cast<float>(idle0_delta) / interval_us)),
        0.0F, 100.0F);
    result.core1 = std::clamp(
        100.0F * (1.0F - (static_cast<float>(idle1_delta) / interval_us)),
        0.0F, 100.0F);
    result.total = (result.core0 + result.core1) * 0.5F;

    previous_time_us = now_us;
    previous_idle0 = idle0;
    previous_idle1 = idle1;
    return result;
}

std::string json_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (character >= 0x20) escaped += static_cast<char>(character);
                break;
        }
    }
    return escaped;
}
}

WebServer::WebServer(AudioPipeline& audio,
                     LosslessServer& lossless,
                     WifiManager& wifi,
                     SonosController& sonos,
                     SonosAutomation& automation)
    : audio_(audio),
      lossless_(lossless),
      wifi_(wifi),
      sonos_(sonos),
      automation_(automation) {}

esp_err_t WebServer::serve_embedded(httpd_req_t* request,
                                    const char* mime,
                                    const uint8_t* begin,
                                    const uint8_t* end) {
    httpd_resp_set_type(request, mime);
    httpd_resp_set_hdr(request, "Cache-Control", "no-cache");
    return httpd_resp_send(
        request, reinterpret_cast<const char*>(begin),
        static_cast<ssize_t>(end - begin));
}

esp_err_t WebServer::index_handler(httpd_req_t* request) {
    return serve_embedded(
        request, "text/html; charset=utf-8",
        reinterpret_cast<const uint8_t*>(web_assets::kIndex),
        reinterpret_cast<const uint8_t*>(web_assets::kIndex) +
            sizeof(web_assets::kIndex) - 1);
}

esp_err_t WebServer::css_handler(httpd_req_t* request) {
    return serve_embedded(
        request, "text/css; charset=utf-8",
        reinterpret_cast<const uint8_t*>(web_assets::kStyle),
        reinterpret_cast<const uint8_t*>(web_assets::kStyle) +
            sizeof(web_assets::kStyle) - 1);
}

esp_err_t WebServer::js_handler(httpd_req_t* request) {
    return serve_embedded(
        request, "application/javascript; charset=utf-8",
        reinterpret_cast<const uint8_t*>(web_assets::kApp),
        reinterpret_cast<const uint8_t*>(web_assets::kApp) +
            sizeof(web_assets::kApp) - 1);
}

esp_err_t WebServer::status_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    const AudioMetrics metrics = self->audio_.metrics();
    const std::string ip = self->wifi_.ip_address();
    const CpuUsage cpu = current_cpu_usage();
    const SonosAutomationStatus automation = self->automation_.status();
    const EqStatus eq = self->audio_.eq_status();
    const WifiLink wifi = self->wifi_.link();
    const ListenerCounts listeners =
        self->lossless_.listener_counts(automation.group_ip.c_str());

    std::array<char, 2304> json = {};
    std::array<char, 40> gains = {};
    size_t gain_length = 0;
    for (const int gain : eq.gains) {
        gain_length += std::snprintf(gains.data() + gain_length,
            gains.size() - gain_length, "%s%d", gain_length == 0 ? "" : ",", gain);
    }
    int offset = std::snprintf(
        json.data(), json.size(),
        "{\"version\":\"%s\",\"uptime_s\":%llu,\"heap_free\":%u,"
        "\"wifi_connected\":%s,\"setup_ap\":%s,\"ip\":\"%s\","
        "\"wifi_rssi\":%d,\"wifi_bssid\":\"%s\",\"wifi_channel\":%u,"
        "\"wifi_reconnects\":%u,"
        "\"lora_enabled\":false,\"lora_reset_asserted\":true,"
        "\"sample_rate\":%u,\"bits\":%u,\"channels\":%u,"
        "\"slot_bits\":%u,\"clock_locked\":%s,\"signal_present\":%s,"
        "\"emphasis\":%s,\"peak_l\":%.2f,\"peak_r\":%.2f,"
        "\"rms_l\":%.2f,\"rms_r\":%.2f,\"captured_frames\":%llu,"
        "\"read_timeouts\":%u,\"overruns\":%u,\"stream_clients\":%u,"
        "\"sonos_stream_clients\":%u,\"other_stream_clients\":%u,"
        "\"dma_queue_overflows\":%u,\"capture_publish_drops\":%u,"
        "\"clock_loss_events\":%u,\"capture_age_ms\":%u,"
        "\"cpu_percent\":%.1f,\"cpu0_percent\":%.1f,\"cpu1_percent\":%.1f,"
        "\"cpu_mhz\":%u,\"capture_stack_free\":%u,"
        "\"analysis_stack_free\":%u,\"analysis_runs\":%u,"
        "\"meter_updates\":%u,\"spectrum_updates\":%u,"
        "\"meter_updated_ms\":%u,\"spectrum_updated_ms\":%u,"
        "\"analysis_dsp_us\":%u,\"analysis_dsp_max_us\":%u,"
        "\"source_rate_hz\":%u,"
        "\"sonos_auto_enabled\":%s,\"sonos_auto_state\":\"%s\","
        "\"sonos_group_ip\":\"%s\",\"sonos_group_name\":\"%s\","
        "\"sonos_play_commanded\":%s,"
        "\"sonos_recovery_attempts\":%u,"
        "\"eq_preset\":\"%s\",\"eq_gains\":[%s],\"eq_headroom_db\":%.2f,"
        "\"eq_applied\":%s,\"eq_clipped_samples\":%u,"
        "\"stream_frames\":%llu,\"stream_url\":\"http://%s:8000/cd.flac\","
        "\"spectrum\":[",
        CDTS_FIRMWARE_VERSION,
        static_cast<unsigned long long>(esp_timer_get_time() / 1000000ULL),
        static_cast<unsigned>(esp_get_free_heap_size()),
        self->wifi_.connected() ? "true" : "false",
        self->wifi_.setup_ap_active() ? "true" : "false", ip.c_str(),
        wifi.rssi, wifi.bssid.data(), wifi.channel, wifi.reconnects,
        static_cast<unsigned>(board::kSampleRate),
        static_cast<unsigned>(board::kBitsPerSample),
        static_cast<unsigned>(board::kChannels),
        static_cast<unsigned>(board::kSlotBits),
        metrics.clock_locked ? "true" : "false",
        metrics.signal_present ? "true" : "false",
        metrics.emphasis ? "true" : "false", metrics.peak_left_dbfs,
        metrics.peak_right_dbfs, metrics.rms_left_dbfs,
        metrics.rms_right_dbfs,
        static_cast<unsigned long long>(metrics.captured_frames),
        static_cast<unsigned>(metrics.read_timeouts),
        static_cast<unsigned>(metrics.overruns),
        static_cast<unsigned>(listeners.sonos + listeners.other),
        static_cast<unsigned>(listeners.sonos),
        static_cast<unsigned>(listeners.other),
        static_cast<unsigned>(metrics.dma_queue_overflows),
        static_cast<unsigned>(metrics.capture_publish_drops),
        static_cast<unsigned>(metrics.clock_loss_events),
        static_cast<unsigned>(metrics.capture_age_ms),
        cpu.total, cpu.core0, cpu.core1,
        static_cast<unsigned>(CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ),
        static_cast<unsigned>(metrics.capture_stack_free),
        static_cast<unsigned>(metrics.analysis_stack_free),
        static_cast<unsigned>(metrics.analysis_runs),
        static_cast<unsigned>(metrics.meter_updates),
        static_cast<unsigned>(metrics.spectrum_updates),
        static_cast<unsigned>(metrics.meter_updated_ms),
        static_cast<unsigned>(metrics.spectrum_updated_ms),
        static_cast<unsigned>(metrics.analysis_dsp_us),
        static_cast<unsigned>(metrics.analysis_dsp_max_us),
        static_cast<unsigned>(metrics.source_rate_hz),
        automation.enabled ? "true" : "false",
        json_escape(automation.state).c_str(),
        json_escape(automation.group_ip).c_str(),
        json_escape(automation.group_name).c_str(),
        automation.play_commanded ? "true" : "false",
        static_cast<unsigned>(automation.recovery_attempts),
        kEqPresetNames[static_cast<size_t>(eq.preset)], gains.data(), eq.headroom_db,
        eq.applied ? "true" : "false", static_cast<unsigned>(eq.clipped_samples),
        static_cast<unsigned long long>(self->lossless_.frames_sent()),
        ip.c_str());

    for (size_t index = 0; index < metrics.spectrum_dbfs.size(); ++index) {
        if (offset < 0 || static_cast<size_t>(offset) >= json.size()) {
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Status response too large");
        }
        offset += std::snprintf(
            json.data() + offset, json.size() - offset, "%s%.1f",
            index == 0 ? "" : ",", metrics.spectrum_dbfs[index]);
    }
    if (offset < 0 || static_cast<size_t>(offset) + 3 > json.size()) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Status response too large");
    }
    std::snprintf(json.data() + offset, json.size() - offset, "]}");

    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json.data());
}

esp_err_t WebServer::meters_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    // Snapshot only: no DSP, Wi-Fi queries, task scans or Sonos locks here.
    const AudioMetrics metrics = self->audio_.meters();
    std::array<char, 768> json = {};
    int offset = std::snprintf(
        json.data(), json.size(),
        "{\"clock_locked\":%s,\"signal_present\":%s,"
        "\"peak_l\":%.2f,\"peak_r\":%.2f,\"rms_l\":%.2f,\"rms_r\":%.2f,"
        "\"meter_updates\":%u,\"spectrum_updates\":%u,"
        "\"meter_updated_ms\":%u,\"spectrum_updated_ms\":%u,"
        "\"capture_age_ms\":%u,\"spectrum\":[",
        metrics.clock_locked ? "true" : "false",
        metrics.signal_present ? "true" : "false",
        metrics.peak_left_dbfs, metrics.peak_right_dbfs,
        metrics.rms_left_dbfs, metrics.rms_right_dbfs,
        static_cast<unsigned>(metrics.meter_updates),
        static_cast<unsigned>(metrics.spectrum_updates),
        static_cast<unsigned>(metrics.meter_updated_ms),
        static_cast<unsigned>(metrics.spectrum_updated_ms),
        static_cast<unsigned>(metrics.capture_age_ms));
    for (size_t index = 0; index < metrics.spectrum_dbfs.size(); ++index) {
        if (offset < 0 || static_cast<size_t>(offset) >= json.size()) {
            return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                       "Meter response too large");
        }
        offset += std::snprintf(json.data() + offset, json.size() - offset,
                                "%s%.1f", index == 0 ? "" : ",",
                                metrics.spectrum_dbfs[index]);
    }
    if (offset < 0 || static_cast<size_t>(offset) + 3 > json.size()) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "Meter response too large");
    }
    std::snprintf(json.data() + offset, json.size() - offset, "]}");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, json.data());
}

bool WebServer::read_form(httpd_req_t* request,
                          char* buffer,
                          size_t capacity) {
    if (request->content_len <= 0 ||
        static_cast<size_t>(request->content_len) >= capacity) {
        return false;
    }
    size_t read = 0;
    while (read < static_cast<size_t>(request->content_len)) {
        const int result = httpd_req_recv(
            request, buffer + read, request->content_len - read);
        if (result <= 0) return false;
        read += static_cast<size_t>(result);
    }
    buffer[read] = '\0';
    return true;
}

esp_err_t WebServer::wifi_scan_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<WifiNetwork, WifiManager::kMaxScanResults> networks = {};
    const size_t count = self->wifi_.scan(networks);

    std::string json = "{\"networks\":[";
    json.reserve(2048);
    for (size_t index = 0; index < count; ++index) {
        if (index != 0) json += ',';
        json += "{\"ssid\":\"" + json_escape(networks[index].ssid) +
                "\",\"rssi\":" + std::to_string(networks[index].rssi) +
                ",\"secure\":" +
                (networks[index].secure ? "true" : "false") + "}";
    }
    json += "]}";
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, json.data(), json.size());
}

bool WebServer::form_value(const char* form,
                           const char* key,
                           char* output,
                           size_t capacity) {
    if (httpd_query_key_value(form, key, output, capacity) != ESP_OK) {
        return false;
    }
    // Decode the two form encodings needed by SSIDs/passwords and IP fields.
    char* write = output;
    for (char* read = output; *read != '\0'; ++read) {
        if (*read == '+') {
            *write++ = ' ';
        } else if (*read == '%' && std::strlen(read) >= 3) {
            unsigned value = 0;
            if (std::sscanf(read + 1, "%2x", &value) == 1) {
                *write++ = static_cast<char>(value);
                read += 2;
            }
        } else {
            *write++ = *read;
        }
    }
    *write = '\0';
    return true;
}

void WebServer::delayed_restart(void*) {
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

esp_err_t WebServer::eq_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<char, 256> form = {};
    char name[16] = {};
    if (!read_form(request, form.data(), form.size()) ||
        !form_value(form.data(), "preset", name, sizeof(name))) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing EQ preset");
    }
    const auto match = std::find_if(kEqPresetNames.begin(), kEqPresetNames.end(),
        [&](const char* preset) { return std::strcmp(preset, name) == 0; });
    if (match == kEqPresetNames.end()) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Unknown EQ preset");
    }
    const auto index = static_cast<size_t>(match - kEqPresetNames.begin());
    const auto preset = static_cast<EqPreset>(index);
    EqGains gains = {};
    if (preset == EqPreset::Custom) {
        for (size_t i = 0; i < gains.size(); ++i) {
            char key[12] = {}, value[8] = {};
            std::snprintf(key, sizeof(key), "gain%u", static_cast<unsigned>(i));
            if (!form_value(form.data(), key, value, sizeof(value))) {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Missing EQ band");
            }
            char* end = nullptr;
            const long gain = std::strtol(value, &end, 10);
            if (end == value || *end != '\0' || gain < -kEqGainLimit || gain > kEqGainLimit) {
                return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "EQ gain must be -6 to +6 dB");
            }
            gains[i] = static_cast<int8_t>(gain);
        }
    } else {
        gains = kEqPresetGains[index];
    }
    self->audio_.set_eq(preset, gains);
    httpd_resp_set_status(request, "204 No Content");
    return httpd_resp_send(request, nullptr, 0);
}

esp_err_t WebServer::wifi_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<char, 256> form = {};
    std::array<char, 33> ssid = {};
    std::array<char, 65> password = {};
    if (!read_form(request, form.data(), form.size()) ||
        !form_value(form.data(), "ssid", ssid.data(), ssid.size()) ||
        !form_value(form.data(), "password", password.data(),
                    password.size()) ||
        !self->wifi_.save_credentials(ssid.data(), password.data())) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "{\"ok\":false}");
    }
    httpd_resp_set_type(request, "application/json");
    const esp_err_t result = httpd_resp_sendstr(
        request, "{\"ok\":true,\"restarting\":true}");
    xTaskCreate(delayed_restart, "restart", 2048, nullptr, 1, nullptr);
    return result;
}

esp_err_t WebServer::sonos_discover_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<SonosDevice, SonosController::kMaxDevices> devices = {};
    const size_t count = self->sonos_.discover(devices);
    const SonosAutomationStatus automation = self->automation_.status();
    std::array<char, 2048> json = {};
    size_t offset = std::snprintf(json.data(), json.size(), "{\"groups\":[");
    for (size_t index = 0; index < count; ++index) {
        const std::string name = json_escape(devices[index].name);
        offset += std::snprintf(
            json.data() + offset, json.size() - offset,
            "%s{\"ip\":\"%s\",\"uuid\":\"%s\",\"name\":\"%s\"}",
            index == 0 ? "" : ",", devices[index].ip.c_str(),
            devices[index].uuid.c_str(), name.c_str());
    }
    std::snprintf(json.data() + offset, json.size() - offset,
                  "],\"selected_ip\":\"%s\"}",
                  automation.group_ip.c_str());
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(request, json.data());
}

esp_err_t WebServer::sonos_play_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<char, 128> form = {};
    std::array<char, 40> player_ip = {};
    if (!read_form(request, form.data(), form.size()) ||
        !form_value(form.data(), "ip", player_ip.data(), player_ip.size())) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "{\"ok\":false}");
    }
    self->automation_.select_group(player_ip.data());
    const bool ok = self->automation_.play_selected();
    if (!ok) httpd_resp_set_status(request, "502 Bad Gateway");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(
        request, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t WebServer::sonos_select_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<char, 384> form = {};
    std::array<char, 40> player_ip = {};
    std::array<char, 80> uuid = {};
    std::array<char, 128> name = {};
    if (!read_form(request, form.data(), form.size()) ||
        !form_value(form.data(), "ip", player_ip.data(), player_ip.size())) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "{\"ok\":false}");
    }
    form_value(form.data(), "uuid", uuid.data(), uuid.size());
    form_value(form.data(), "name", name.data(), name.size());
    const bool ok =
        self->automation_.select_group(player_ip.data(), uuid.data(),
                                       name.data());
    if (!ok) httpd_resp_set_status(request, "500 Internal Server Error");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(
        request, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

esp_err_t WebServer::sonos_stop_handler(httpd_req_t* request) {
    auto* self = static_cast<WebServer*>(request->user_ctx);
    std::array<char, 128> form = {};
    std::array<char, 40> player_ip = {};
    if (!read_form(request, form.data(), form.size()) ||
        !form_value(form.data(), "ip", player_ip.data(), player_ip.size())) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_sendstr(request, "{\"ok\":false}");
    }
    self->automation_.select_group(player_ip.data());
    const bool ok = self->automation_.stop_selected();
    if (!ok) httpd_resp_set_status(request, "502 Bad Gateway");
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_sendstr(
        request, ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

bool WebServer::start() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 13;
    config.stack_size = 7168;
    config.lru_purge_enable = true;
    if (httpd_start(&server_, &config) != ESP_OK) {
        return false;
    }

    const std::array<httpd_uri_t, 13> routes = {{
        {"/", HTTP_GET, index_handler, this},
        {"/style.css", HTTP_GET, css_handler, this},
        {"/app.js", HTTP_GET, js_handler, this},
        {"/api/status", HTTP_GET, status_handler, this},
        {"/api/meters", HTTP_GET, meters_handler, this},
        {"/api/eq", HTTP_POST, eq_handler, this},
        {"/api/wifi/scan", HTTP_GET, wifi_scan_handler, this},
        {"/api/wifi", HTTP_POST, wifi_handler, this},
        {"/api/sonos/discover", HTTP_GET, sonos_discover_handler, this},
        {"/api/sonos/select", HTTP_POST, sonos_select_handler, this},
        {"/api/sonos/play", HTTP_POST, sonos_play_handler, this},
        {"/api/sonos/stop", HTTP_POST, sonos_stop_handler, this},
        {"/index.html", HTTP_GET, index_handler, this},
    }};
    for (const auto& route : routes) {
        if (httpd_register_uri_handler(server_, &route) != ESP_OK) {
            ESP_LOGE(kTag, "Failed to register %s", route.uri);
            return false;
        }
    }
    ESP_LOGI(kTag, "Dashboard ready on port 80");
    return true;
}

}  // namespace cdts
