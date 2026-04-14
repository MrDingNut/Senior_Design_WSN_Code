#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_wifi_types.h"

static const char *TAG = "node_esp";

// *** Paste your Base Station ESP's MAC address here ***
// Flash base_station_esp first and check its serial output for the MAC address
static uint8_t base_station_mac[6] = {0x90, 0x70, 0x69, 0x06, 0xF4, 0x0C};

#define ESPNOW_CHANNEL 1

typedef struct {
    float moisture;
    float ph;
    float ec;
    float temp;
    float n;
    float p;
    float k;
} sensor_data_t;

static float rand_float(float min, float max) {
    return min + ((float)(esp_random() % 10000) / 10000.0f) * (max - min);
}

static void send_cb(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        ESP_LOGW(TAG, "Send failed");
    }
}

void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    // Initialize WiFi
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE));

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_send_cb(send_cb);

    // Register base station as a peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, base_station_mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "Node ESP ready, sending data every 2 seconds");

    while (1) {
        sensor_data_t data = {
            .moisture = rand_float(  0.0f, 100.0f),
            .ph       = rand_float(  4.0f,   9.0f),
            .ec       = rand_float(  0.0f,   5.0f),
            .temp     = rand_float( 10.0f,  40.0f),
            .n        = rand_float(  0.0f, 200.0f),
            .p        = rand_float(  0.0f, 200.0f),
            .k        = rand_float(  0.0f, 200.0f),
        };

        esp_now_send(base_station_mac, (uint8_t *)&data, sizeof(data));

        ESP_LOGI(TAG, "Sent: moisture=%.1f ph=%.2f ec=%.3f temp=%.1f N=%.1f P=%.1f K=%.1f",
                 data.moisture, data.ph, data.ec, data.temp, data.n, data.p, data.k);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
