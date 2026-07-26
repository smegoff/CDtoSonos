#include <memory>

#include "audio_pipeline.hpp"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lossless_server.hpp"
#include "nvs_flash.h"
#include "oled_display.hpp"
#include "radio_guard.hpp"
#include "sonos.hpp"
#include "sonos_automation.hpp"
#include "web_server.hpp"
#include "wifi_manager.hpp"

namespace {
constexpr char kTag[] = "cdtosonos";
}

extern "C" void app_main() {
    // Safety invariant: no service, task, SPI peripheral or network component
    // starts before the SX1262 has been forced into hardware reset.
    cdts::disable_lora_radio();

    esp_err_t nvs_result = nvs_flash_init();
    if (nvs_result == ESP_ERR_NVS_NO_FREE_PAGES ||
        nvs_result == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_result = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_result);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    static cdts::WifiManager wifi;
    static cdts::AudioPipeline audio;
    static cdts::LosslessServer lossless(audio);
    static cdts::SonosController sonos;
    static cdts::SonosAutomation automation(audio, lossless, wifi, sonos);
    static cdts::WebServer web(audio, lossless, wifi, sonos, automation);
    static cdts::OledDisplay display(audio, lossless, wifi);

    ESP_ERROR_CHECK(wifi.start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(audio.start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(lossless.start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(web.start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(display.start() ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(automation.start() ? ESP_OK : ESP_FAIL);

    ESP_LOGI(kTag,
             "Firmware %s started: lossless FLAC, LoRa permanently disabled",
             CDTS_FIRMWARE_VERSION);
}
