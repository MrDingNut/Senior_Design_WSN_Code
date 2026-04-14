#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "base_station_esp";

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

static void recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
    if (len != sizeof(sensor_data_t)) {
        ESP_LOGW(TAG, "Unexpected packet size: %d (expected %d)", len, sizeof(sensor_data_t));
        return;
    }

    sensor_data_t reading;
    memcpy(&reading, data, sizeof(reading));

    // Print as JSON for the Python base station to parse
    printf("{\"moisture\":%.1f,\"ph\":%.2f,\"ec\":%.3f,\"temp\":%.1f,\"n\":%.1f,\"p\":%.1f,\"k\":%.1f}\n",
           reading.moisture, reading.ph, reading.ec,
           reading.temp, reading.n, reading.p, reading.k);
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

    // Print MAC address — copy this into node_esp's base_station_mac
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));
    ESP_LOGI(TAG, "Base Station MAC: %02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    // Initialize ESP-NOW
    ESP_ERROR_CHECK(esp_now_init());
    esp_now_register_recv_cb(recv_cb);

    ESP_LOGI(TAG, "Waiting for data from Node ESP...");

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
