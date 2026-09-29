/**
 * @file main.c
 * @brief 从板 N1：8 路输出，取 DATA data[0]（bits 0–7）为命令
 *
 * 键 1–8 ↔ data[0] 的 bit0–bit7 ↔ s_pins[0..7]
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "driver/twai.h"
#include "esp_err.h"
#include "esp_log.h"

#define CAN_TX_GPIO              GPIO_NUM_9
#define CAN_RX_GPIO              GPIO_NUM_10

#define CAN_ID_ALL_OFF           0x000
#define CAN_ID_HOST_BROADCAST    0x100
#define CAN_ID_REPORT_BASE       0x200

#define MSG_DISCOVER             0x01
#define MSG_SYNC                 0x02
#define MSG_SYSTEM_CTRL          0x04
#define MSG_ALL_OFF              0x05
#define MSG_DEVICE_REPORT        0x81

#define NODE_ID                  1
#define DATA_BYTE_INDEX          0   /* 本板取 data[0] */
#define OUTPUT_CHANNEL_COUNT     8
#define FW_VER_MAJOR             1
#define FW_VER_MINOR             2

#define RECEIVE_WAIT_MS          50
#define HEARTBEAT_PERIOD_MS      1000

static const gpio_num_t s_pins[OUTPUT_CHANNEL_COUNT] = {
    GPIO_NUM_1, GPIO_NUM_2, GPIO_NUM_4, GPIO_NUM_5,
    GPIO_NUM_6, GPIO_NUM_7, GPIO_NUM_8, GPIO_NUM_11
};

static const char *TAG = "NODE_N1";
static uint8_t s_mask = 0;
static bool s_playing = false;

static void print_frame(const char *dir, const twai_message_t *m)
{
    printf("%s ID=0x%03" PRIX32 " DLC=%u DATA=",
           dir, m->identifier, m->data_length_code);
    for (int i = 0; i < m->data_length_code; i++) {
        printf("%02X ", m->data[i]);
    }
    printf("\n");
}

static esp_err_t gpio_init_outputs(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < OUTPUT_CHANNEL_COUNT; i++) {
        mask |= (1ULL << s_pins[i]);
    }
    gpio_config_t cfg = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    for (int i = 0; i < OUTPUT_CHANNEL_COUNT; i++) {
        gpio_set_level(s_pins[i], 0);
    }
    s_mask = 0;
    return ESP_OK;
}

static void apply_mask8(uint8_t mask)
{
    s_mask = mask;
    for (int i = 0; i < OUTPUT_CHANNEL_COUNT; i++) {
        gpio_set_level(s_pins[i], (mask >> i) & 0x1);
    }
    ESP_LOGI(TAG, "CMD=0x%02X (data[%d], keys 1-8)", s_mask, DATA_BYTE_INDEX);
}

static esp_err_t can_init(void)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    g.tx_queue_len = 8;
    g.rx_queue_len = 16;
    g.alerts_enabled = TWAI_ALERT_NONE;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g, &t, &f);
    if (err != ESP_OK) {
        return err;
    }
    return twai_start();
}

static esp_err_t send_device_report(void)
{
    twai_message_t msg = {0};
    msg.identifier = CAN_ID_REPORT_BASE + NODE_ID;
    msg.data_length_code = 6;
    msg.data[0] = MSG_DEVICE_REPORT;
    msg.data[1] = NODE_ID;
    msg.data[2] = OUTPUT_CHANNEL_COUNT; /* 8 */
    msg.data[3] = FW_VER_MAJOR;
    msg.data[4] = FW_VER_MINOR;
    msg.data[5] = s_playing ? 0x01 : 0x00;

    vTaskDelay(pdMS_TO_TICKS(10 * NODE_ID));
    esp_err_t err = twai_transmit(&msg, pdMS_TO_TICKS(100));
    if (err == ESP_OK) {
        print_frame("[CAN TX]", &msg);
    }
    return err;
}

static void handle_host_frame(const twai_message_t *m)
{
    /* KEY_STATE: DLC=8，取本板 1 字节 */
    if (m->data_length_code == 8) {
        apply_mask8(m->data[DATA_BYTE_INDEX]);
        return;
    }
    if (m->data_length_code == 2 && m->data[0] == MSG_DISCOVER) {
        send_device_report();
        return;
    }
    if (m->data_length_code == 6 && m->data[0] == MSG_SYNC) {
        ESP_LOGI(TAG, "SYNC");
        return;
    }
    if (m->data_length_code == 3 && m->data[0] == MSG_SYSTEM_CTRL) {
        uint8_t cmd = m->data[1];
        if (cmd == 1) {
            s_playing = true;
        } else if (cmd == 0) {
            s_playing = false;
        }
        ESP_LOGI(TAG, "SYSTEM_CTRL %u", cmd);
    }
}

void app_main(void)
{
    if (gpio_init_outputs() != ESP_OK || can_init() != ESP_OK) {
        ESP_LOGE(TAG, "init failed");
        return;
    }

    ESP_LOGI(TAG, "N%d: 8ch, use data[%d]", NODE_ID, DATA_BYTE_INDEX);

    uint32_t rx_count = 0;
    TickType_t last_hb = xTaskGetTickCount();

    while (1) {
        twai_message_t rx = {0};
        if (twai_receive(&rx, pdMS_TO_TICKS(RECEIVE_WAIT_MS)) == ESP_OK &&
            !rx.extd && !rx.rtr) {
            rx_count++;
            print_frame("[CAN RX]", &rx);

            if (rx.identifier == CAN_ID_ALL_OFF) {
                if (rx.data_length_code >= 1 && rx.data[0] == MSG_ALL_OFF) {
                    apply_mask8(0);
                }
            } else if (rx.identifier == CAN_ID_HOST_BROADCAST) {
                handle_host_frame(&rx);
            }
        }

        TickType_t now = xTaskGetTickCount();
        if ((now - last_hb) >= pdMS_TO_TICKS(HEARTBEAT_PERIOD_MS)) {
            last_hb = now;
            ESP_LOGI(TAG, "[ALIVE] RX=%" PRIu32 " MASK=0x%02X", rx_count, s_mask);
        }
    }
}
