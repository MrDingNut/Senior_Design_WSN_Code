#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "esp_log.h"
#include "esp_wifi_types.h"
#include "driver/uart.h"

static const char *TAG = "node_esp";

// *** Paste your Base Station ESP's MAC address here ***
static uint8_t base_station_mac[6] = {0x90, 0x70, 0x69, 0x06, 0xF4, 0x0C};

#define ESPNOW_CHANNEL 1

// RS-485 / Modbus RTU config
#define RS485_UART_NUM  UART_NUM_1
#define RS485_TX_PIN    4
#define RS485_RX_PIN    5
#define RS485_BAUD      9600

#define MODBUS_SLAVE_ID 0x01

typedef struct {
    float moisture;
    float ph;
    float ec;
    float temp;
    float n;
    float p;
    float k;
} sensor_data_t;

// Register addresses and scaling factors (pH, Moisture, Temp, EC, N, P, K)
static const uint16_t MB_REGS[]  = {0x0006, 0x0012, 0x0013, 0x0015, 0x001E, 0x001F, 0x0020};
static const float    MB_SCALE[] = {0.01f,  0.1f,   0.1f,   1.0f,   1.0f,   1.0f,   1.0f  };

static uint16_t crc16(const uint8_t *data, int len) {
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

static bool read_register(uint16_t reg, float scale, float *out) {
    uint8_t req[8];
    req[0] = MODBUS_SLAVE_ID;
    req[1] = 0x03;
    req[2] = reg >> 8;
    req[3] = reg & 0xFF;
    req[4] = 0x00;
    req[5] = 0x01;
    uint16_t c = crc16(req, 6);
    req[6] = c & 0xFF;
    req[7] = c >> 8;

    uart_flush_input(RS485_UART_NUM);
    uart_write_bytes(RS485_UART_NUM, (const char *)req, 8);
    uart_wait_tx_done(RS485_UART_NUM, pdMS_TO_TICKS(100));

    vTaskDelay(pdMS_TO_TICKS(30));

    uint8_t resp[7];
    int received = uart_read_bytes(RS485_UART_NUM, resp, 7, pdMS_TO_TICKS(300));
    if (received < 7) return false;

    uint16_t crc_calc = crc16(resp, 5);
    uint16_t crc_recv = resp[5] | (resp[6] << 8);
    if (crc_calc != crc_recv) return false;

    *out = ((resp[3] << 8) | resp[4]) * scale;
    return true;
}

static bool poll_sensors(sensor_data_t *d) {
    float vals[7];
    for (int i = 0; i < 7; i++) {
        if (!read_register(MB_REGS[i], MB_SCALE[i], &vals[i])) {
            ESP_LOGW(TAG, "Failed to read register 0x%04X", MB_REGS[i]);
            return false;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // vals order: pH, Moisture, Temp, EC, N, P, K
    d->ph       = vals[0];
    d->moisture = vals[1];
    d->temp     = vals[2];
    d->ec       = vals[3];
    d->n        = vals[4];
    d->p        = vals[5];
    d->k        = vals[6];
    return true;
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

    // Initialize UART for RS-485 Modbus
    uart_config_t uart_cfg = {
        .baud_rate  = RS485_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    ESP_ERROR_CHECK(uart_param_config(RS485_UART_NUM, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(RS485_UART_NUM, RS485_TX_PIN, RS485_RX_PIN,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(RS485_UART_NUM, 256, 0, 0, NULL, 0));

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

    // Register base station as peer
    esp_now_peer_info_t peer = {};
    memcpy(peer.peer_addr, base_station_mac, 6);
    peer.channel = ESPNOW_CHANNEL;
    peer.encrypt = false;
    ESP_ERROR_CHECK(esp_now_add_peer(&peer));

    ESP_LOGI(TAG, "Node ESP ready, polling sensor every 10 seconds");

    while (1) {
        sensor_data_t data;
        if (poll_sensors(&data)) {
            esp_now_send(base_station_mac, (uint8_t *)&data, sizeof(data));
            ESP_LOGI(TAG, "Sent: moisture=%.1f ph=%.2f ec=%.3f temp=%.1f N=%.1f P=%.1f K=%.1f",
                     data.moisture, data.ph, data.ec, data.temp, data.n, data.p, data.k);
        } else {
            ESP_LOGE(TAG, "Sensor read failed, skipping transmission");
        }

        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
