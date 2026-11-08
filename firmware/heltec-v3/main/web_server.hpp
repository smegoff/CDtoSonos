#pragma once

#include "audio_pipeline.hpp"
#include "esp_http_server.h"
#include "lossless_server.hpp"
#include "sonos.hpp"
#include "sonos_automation.hpp"
#include "wifi_manager.hpp"

namespace cdts {

class WebServer {
public:
    WebServer(AudioPipeline& audio,
              LosslessServer& lossless,
              WifiManager& wifi,
              SonosController& sonos,
              SonosAutomation& automation);
    bool start();

private:
    static esp_err_t index_handler(httpd_req_t* request);
    static esp_err_t css_handler(httpd_req_t* request);
    static esp_err_t js_handler(httpd_req_t* request);
    static esp_err_t status_handler(httpd_req_t* request);
    static esp_err_t meters_handler(httpd_req_t* request);
    static esp_err_t eq_handler(httpd_req_t* request);
    static esp_err_t wifi_scan_handler(httpd_req_t* request);
    static esp_err_t wifi_handler(httpd_req_t* request);
    static esp_err_t sonos_discover_handler(httpd_req_t* request);
    static esp_err_t sonos_select_handler(httpd_req_t* request);
    static esp_err_t sonos_play_handler(httpd_req_t* request);
    static esp_err_t sonos_stop_handler(httpd_req_t* request);
    static esp_err_t serve_embedded(httpd_req_t* request,
                                    const char* mime,
                                    const uint8_t* begin,
                                    const uint8_t* end);
    static bool read_form(httpd_req_t* request,
                          char* buffer,
                          size_t capacity);
    static bool form_value(const char* form,
                           const char* key,
                           char* output,
                           size_t capacity);
    static void delayed_restart(void*);

    AudioPipeline& audio_;
    LosslessServer& lossless_;
    WifiManager& wifi_;
    SonosController& sonos_;
    SonosAutomation& automation_;
    httpd_handle_t server_ = nullptr;
};

}  // namespace cdts
