/**
 * @file main.c
 * @brief 主板：短窗位图 UART → 单帧 CAN 广播 + Bus-Off 恢复 + ACK
 *
 * UART（热路径）：
 *   @ST,<seq4hex>,<t_ms8hex>,<bitmap8hex>,<crc4hex>\n
 *   CRC16-CCITT-FALSE 覆盖 "ST,<seq>,<t>,<bitmap>"（不含 @ 与 CRC 字段）
 * 兼容：ON/OFF/SET/ALL_OFF/DEBUG/DISCOVER/SYNC/...
 *
 * CAN 仲裁 ID（标准帧，ID 越小优先级越高）：
 *   0x000 ALL_OFF（最高）
 *   0x100 KEY_STATE 位图广播 DLC=4 大端
 *   0x100 其它控制仍用不同 DLC 区分（DISCOVER=2, SYNC=6, SYS=3）
 *   0x201+ 从板上报（低优先级，不与键位抢总线）
 *
 * KEY_STATE 载荷（大端 uint32）：
 *   data[0]=bits31..24(N4) data[1]=bits23..16(N3)
 *   data[2]=bits15..8(N2)  data[3]=bits7..0(N1)
 * 从板 k 取：data[3-k] 或 (bitmap>>(8*k))&0xFF
 *
 * TX_SUCCESS 后回：@OK,<seq>,0\n ；失败回 @ERR,<seq>,<code>\n
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/twai.h"
#include "driver/uart.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_dev.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#define CAN_TX_GPIO              GPIO_NUM_9
#define CAN_RX_GPIO              GPIO_NUM_10

/** 仲裁优先级：急停 > 键位状态；从板上报用更高 ID */
#define CAN_ID_ALL_OFF           0x000
#define CAN_ID_KEY_STATE         0x100  /* 状态广播，高优先级 */
#define CAN_ID_HOST_CTRL         0x100  /* 同 ID，靠 DLC 区分管理帧 */
#define CAN_ID_REPORT_N1         0x201
#define CAN_ID_REPORT_N2         0x202

#define MSG_DISCOVER             0x01
#define MSG_SYNC                 0x02
#define MSG_SYSTEM_CTRL          0x04
#define MSG_ALL_OFF              0x05
#define MSG_DEVICE_REPORT        0x81

#define SYS_CMD_STOP             0
#define SYS_CMD_START            1
#define SYS_CMD_PAUSE            2

#define MAGNET_COUNT             16
#define CHANNELS_PER_NODE        8

/** ESP 侧二次合并窗：连续 ON/OFF 在此时间内只发一帧 CAN */
#define COALESCE_WINDOW_US       (3000ULL)

#define TX_RETRY_MAX             3

#if defined(CONFIG_ESP_CONSOLE_UART_NUM)
#define CMD_UART_NUM             CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CMD_UART_NUM             UART_NUM_0
#endif

static const char *TAG = "CAN_HOST";

/** 目标位图 bit0=键1 …（预留 32，当前用低 16） */
static uint32_t s_key_state = 0;
/** 最近一次 CAN 发送成功确认的位图 */
static uint32_t s_confirmed_state = 0;
static uint8_t s_msg_seq = 0;
static uint8_t s_discover_seq = 0;
static uint8_t s_play_seq = 0;
static bool s_playing = false;
static bool s_uart_debug = false;

/** 待发送脏标记与时间戳（ESP 侧短窗） */
static bool s_dirty = false;
static int64_t s_dirty_since_us = 0;
static uint16_t s_last_uart_seq = 0;

static uint8_t next_seq(void)
{
    return ++s_msg_seq;
}

static void uart_report(const char *fmt, ...)
{
    if (!s_uart_debug) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

/** CRC-16/CCITT-FALSE */
static uint16_t crc16_ccitt(const char *data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)((uint8_t)data[i]) << 8;
        for (int b = 0; b < 8; b++) {
            if (crc & 0x8000) {
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

static esp_err_t cmd_uart_init(void)
{
    /* RX 1024，TX 256：避免热路径偶发打印堵死 */
    esp_err_t err = uart_driver_install(CMD_UART_NUM, 1024, 256, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "UART install fail: %s", esp_err_to_name(err));
        return err;
    }
    esp_vfs_dev_uart_use_driver(CMD_UART_NUM);
    return ESP_OK;
}

static int cmd_uart_read_line(char *buf, size_t buf_len, uint32_t wait_ms)
{
    size_t idx = 0;
    TickType_t start = xTaskGetTickCount();
    TickType_t budget = pdMS_TO_TICKS(wait_ms);
    while (idx < buf_len - 1) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= budget && idx == 0) {
            return 0; /* 超时且无数据：留给 coalesce 冲洗 */
        }
        TickType_t slice = pdMS_TO_TICKS(20);
        if (budget > elapsed && (budget - elapsed) < slice) {
            slice = budget - elapsed;
        }
        uint8_t ch = 0;
        int n = uart_read_bytes(CMD_UART_NUM, &ch, 1, slice);
        if (n != 1) {
            if (idx == 0) {
                return 0;
            }
            continue;
        }
        if (ch == '\r' || ch == '\n') {
            if (idx == 0) {
                continue;
            }
            buf[idx] = '\0';
            if (s_uart_debug) {
                printf("\r\n");
                fflush(stdout);
            }
            return (int)idx;
        }
        if (ch == 0x08 || ch == 0x7F) {
            if (idx > 0) {
                idx--;
            }
            continue;
        }
        if (ch < 0x20) {
            continue;
        }
        buf[idx++] = (char)ch;
        if (s_uart_debug) {
            printf("%c", ch);
            fflush(stdout);
        }
        start = xTaskGetTickCount(); /* 有数据则延长行组装 */
        budget = pdMS_TO_TICKS(200);
    }
    buf[buf_len - 1] = '\0';
    return (int)(buf_len - 1);
}

static void print_frame(const char *dir, const twai_message_t *m)
{
    if (!s_uart_debug) {
        return;
    }
    printf("%s ID=0x%03" PRIX32 " DLC=%u DATA=",
           dir, m->identifier, m->data_length_code);
    for (int i = 0; i < m->data_length_code; i++) {
        printf("%02X ", m->data[i]);
    }
    printf("\n");
}

/**
 * 处理 TWAI 告警：Bus-Off 时发起恢复并等待回到 Running。
 * @return true 驱动可用；false 仍不可用
 */
static bool can_ensure_running(void)
{
    uint32_t alerts = 0;
    if (twai_read_alerts(&alerts, 0) == ESP_OK) {
        if (alerts & TWAI_ALERT_BUS_OFF) {
            uart_report("CAN bus-off, recovering...\r\n");
            twai_initiate_recovery();
            /* 等待恢复完成（最多约 200ms） */
            for (int i = 0; i < 40; i++) {
                alerts = 0;
                twai_read_alerts(&alerts, pdMS_TO_TICKS(5));
                if (alerts & TWAI_ALERT_BUS_RECOVERED) {
                    twai_start();
                    uart_report("CAN recovered\r\n");
                    return true;
                }
                twai_status_info_t st;
                if (twai_get_status_info(&st) == ESP_OK &&
                    st.state == TWAI_STATE_RUNNING) {
                    return true;
                }
            }
            /* 兜底：停启驱动 */
            twai_stop();
            vTaskDelay(pdMS_TO_TICKS(10));
            if (twai_start() == ESP_OK) {
                uart_report("CAN restart ok\r\n");
                return true;
            }
            return false;
        }
        if (alerts & (TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
                      TWAI_ALERT_TX_FAILED | TWAI_ALERT_ARB_LOST)) {
            uart_report("CAN alert=0x%08" PRIx32 "\r\n", alerts);
        }
    }

    twai_status_info_t st;
    if (twai_get_status_info(&st) == ESP_OK) {
        if (st.state == TWAI_STATE_BUS_OFF) {
            twai_initiate_recovery();
            vTaskDelay(pdMS_TO_TICKS(50));
            twai_start();
            return true;
        }
        if (st.state == TWAI_STATE_STOPPED) {
            return twai_start() == ESP_OK;
        }
        if (st.state != TWAI_STATE_RUNNING) {
            return false;
        }
    }
    return true;
}

static esp_err_t can_init(void)
{
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        CAN_TX_GPIO, CAN_RX_GPIO, TWAI_MODE_NORMAL);
    g.tx_queue_len = 8;   /* 单帧位图为主，队列不必过深 */
    g.rx_queue_len = 16;
    g.alerts_enabled = TWAI_ALERT_BUS_OFF | TWAI_ALERT_BUS_RECOVERED |
                       TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_ERROR |
                       TWAI_ALERT_TX_FAILED | TWAI_ALERT_ARB_LOST |
                       TWAI_ALERT_TX_SUCCESS;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    twai_filter_config_t f = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&g, &t, &f);
    if (err != ESP_OK) {
        return err;
    }
    err = twai_start();
    if (err != ESP_OK) {
        twai_driver_uninstall();
        return err;
    }
    return ESP_OK;
}

static esp_err_t can_send(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    if (!can_ensure_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    twai_message_t msg = {0};
    msg.identifier = id;
    msg.extd = 0;
    msg.rtr = 0;
    msg.data_length_code = dlc;
    memcpy(msg.data, data, dlc);

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < TX_RETRY_MAX; attempt++) {
        err = twai_transmit(&msg, pdMS_TO_TICKS(50));
        if (err == ESP_OK) {
            /* 等待本帧 TX_SUCCESS（链路层 ACK 槽确认） */
            uint32_t alerts = 0;
            if (twai_read_alerts(&alerts, pdMS_TO_TICKS(50)) == ESP_OK) {
                if (alerts & TWAI_ALERT_TX_FAILED) {
                    err = ESP_FAIL;
                    can_ensure_running();
                    continue;
                }
            }
            print_frame("[CAN TX]", &msg);
            return ESP_OK;
        }
        if (s_uart_debug) {
            ESP_LOGE(TAG, "TX fail(%d): %s", attempt, esp_err_to_name(err));
        }
        can_ensure_running();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return err;
}

/**
 * 广播完整位图：DLC=4 大端。仲裁 ID=0x100（高于从板 0x201+）。
 */
static esp_err_t broadcast_key_state(void)
{
    uint8_t data[4] = {
        (uint8_t)((s_key_state >> 24) & 0xFF),
        (uint8_t)((s_key_state >> 16) & 0xFF),
        (uint8_t)((s_key_state >> 8) & 0xFF),
        (uint8_t)(s_key_state & 0xFF)
    };
    uart_report("KEY=0x%08" PRIX32 " N1=0x%02X N2=0x%02X\r\n",
                s_key_state, data[3], data[2]);
    esp_err_t err = can_send(CAN_ID_KEY_STATE, data, 4);
    if (err == ESP_OK) {
        s_confirmed_state = s_key_state;
    }
    return err;
}

static void mark_dirty(void)
{
    if (!s_dirty) {
        s_dirty_since_us = esp_timer_get_time();
    }
    s_dirty = true;
}

static void flush_if_due(bool force)
{
    if (!s_dirty) {
        return;
    }
    if (!force) {
        int64_t now = esp_timer_get_time();
        if ((now - s_dirty_since_us) < (int64_t)COALESCE_WINDOW_US) {
            return;
        }
    }
    /* 与上次成功帧相同则跳过，避免空转占总线 */
    if (s_key_state == s_confirmed_state) {
        s_dirty = false;
        return;
    }
    esp_err_t err = broadcast_key_state();
    s_dirty = false;
    if (s_last_uart_seq != 0) {
        if (err == ESP_OK) {
            printf("@OK,%04X,0\r\n", s_last_uart_seq);
        } else {
            printf("@ERR,%04X,%d\r\n", s_last_uart_seq, (int)err);
        }
        fflush(stdout);
        s_last_uart_seq = 0;
    }
}

static esp_err_t broadcast_all_off(void)
{
    s_key_state = 0;
    s_dirty = false;
    uint8_t data[2] = {MSG_ALL_OFF, next_seq()};
    esp_err_t err = can_send(CAN_ID_ALL_OFF, data, 2);
    if (err == ESP_OK) {
        s_confirmed_state = 0;
    } else {
        /* 退化为位图全 0 */
        err = broadcast_key_state();
    }
    return err;
}

static esp_err_t broadcast_discover(void)
{
    s_discover_seq++;
    uint8_t data[2] = {MSG_DISCOVER, s_discover_seq};
    return can_send(CAN_ID_HOST_CTRL, data, 2);
}

static esp_err_t broadcast_sync(void)
{
    uint32_t ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    uint8_t data[6] = {
        MSG_SYNC,
        next_seq(),
        (uint8_t)(ms & 0xFF),
        (uint8_t)((ms >> 8) & 0xFF),
        (uint8_t)((ms >> 16) & 0xFF),
        (uint8_t)((ms >> 24) & 0xFF)
    };
    return can_send(CAN_ID_HOST_CTRL, data, 6);
}

static esp_err_t broadcast_system_ctrl(uint8_t cmd)
{
    if (cmd == SYS_CMD_START) {
        s_playing = true;
        s_play_seq++;
    } else if (cmd == SYS_CMD_STOP) {
        s_playing = false;
    }
    uint8_t data[3] = {MSG_SYSTEM_CTRL, cmd, s_play_seq};
    return can_send(CAN_ID_HOST_CTRL, data, 3);
}

static void collect_reports_ms(uint32_t wait_ms)
{
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(wait_ms);
    while (xTaskGetTickCount() < deadline) {
        twai_message_t rx = {0};
        TickType_t left = deadline - xTaskGetTickCount();
        if (left == 0) {
            break;
        }
        if (twai_receive(&rx, left) != ESP_OK) {
            break;
        }
        if (rx.extd || rx.rtr) {
            continue;
        }
        if (rx.identifier >= CAN_ID_REPORT_N1 &&
            rx.identifier <= CAN_ID_REPORT_N2) {
            print_frame("[CAN RX]", &rx);
            if (rx.data_length_code >= 6 && rx.data[0] == MSG_DEVICE_REPORT) {
                uart_report("DEVICE_REPORT NODE=%u CH=%u VER=%u.%u\r\n",
                            rx.data[1], rx.data[2], rx.data[3], rx.data[4]);
            }
        }
    }
}

static void trim_line(char *line)
{
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' ||
                     line[n - 1] == ' ' || line[n - 1] == '\t')) {
        line[--n] = '\0';
    }
    char *s = line;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (s != line) {
        memmove(line, s, strlen(s) + 1);
    }
    for (char *p = line; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
}

static void print_help(void)
{
    printf(
        "\r\n=== HOST bitmap broadcast ===\r\n"
        "  @ST,seq,t_ms,bitmap8,crc16\r\n"
        "  ON/OFF/SET <n>...  ALL_OFF  STATUS  DEBUG ON|OFF\r\n"
        "  DISCOVER SYNC START|STOP|PAUSE HELP\r\n"
        "CAN ID 0x100 DLC=4 BE bitmap; slaves use data[3-board]\r\n\r\n"
    );
}

static void print_status(void)
{
    printf("KEY=0x%08" PRIX32 " CONF=0x%08" PRIX32 "\r\n",
           s_key_state, s_confirmed_state);
    printf("ON:");
    int any = 0;
    for (int i = 0; i < MAGNET_COUNT; i++) {
        if (s_key_state & (1u << i)) {
            printf(" %d", i + 1);
            any = 1;
        }
    }
    if (!any) {
        printf(" (none)");
    }
    printf("\r\n");
}

static int parse_key_list(char *args, int *ids, int max_ids)
{
    int count = 0;
    char *token = strtok(args, " ,;\t");
    while (token) {
        char *end = NULL;
        long v = strtol(token, &end, 10);
        if (end == token || *end || v < 1 || v > MAGNET_COUNT) {
            uart_report("ERR id 1..%d got '%s'\r\n", MAGNET_COUNT, token);
            return -1;
        }
        if (count >= max_ids) {
            return -1;
        }
        ids[count++] = (int)v;
        token = strtok(NULL, " ,;\t");
    }
    return count > 0 ? count : -1;
}

/**
 * 解析 @ST,seq,t,bitmap,crc
 * @return 0 成功；-1 失败
 */
static int handle_st_frame(char *line)
{
    /* line 已 uppercase，形如 @ST,002A,0001E240,80000103,A1B2 */
    if (strncmp(line, "@ST,", 4) != 0) {
        return -1;
    }
    char *p = line + 4;
    char *fields[4] = {0};
    int nf = 0;
    fields[nf++] = p;
    while (*p && nf < 4) {
        if (*p == ',') {
            *p = '\0';
            fields[nf++] = p + 1;
        }
        p++;
    }
    if (nf != 4) {
        printf("@ERR,0000,1\r\n");
        return -1;
    }

    /* 校验 CRC：body = ST,seq,t,bitmap */
    char body[64];
    snprintf(body, sizeof(body), "ST,%s,%s,%s",
             fields[0], fields[1], fields[2]);
    uint16_t expect = (uint16_t)strtoul(fields[3], NULL, 16);
    uint16_t got = crc16_ccitt(body, strlen(body));
    if (expect != got) {
        printf("@ERR,%s,2\r\n", fields[0]); /* CRC */
        return -1;
    }

    uint16_t seq = (uint16_t)strtoul(fields[0], NULL, 16);
    uint32_t bitmap = (uint32_t)strtoul(fields[2], NULL, 16);
    /* 当前硬件只驱动低 16 位；高位保留不影响 N1/N2 */
    s_key_state = bitmap;
    s_last_uart_seq = seq;
    mark_dirty();
    /* ST 已是聚合快照：立即刷出，不再等 ESP 窗 */
    flush_if_due(true);
    return 0;
}

static void handle_line(char *line)
{
    trim_line(line);
    if (!line[0]) {
        return;
    }

    if (line[0] == '@' && !strncmp(line, "@ST,", 4)) {
        handle_st_frame(line);
        return;
    }

    if (!strcmp(line, "HELP") || !strcmp(line, "?")) {
        print_help();
        return;
    }
    if (!strcmp(line, "DEBUG ON") || !strcmp(line, "DEBUG 1")) {
        s_uart_debug = true;
        printf("DEBUG=1\r\n");
        return;
    }
    if (!strcmp(line, "DEBUG OFF") || !strcmp(line, "DEBUG 0")) {
        printf("DEBUG=0\r\n");
        s_uart_debug = false;
        return;
    }
    if (!strcmp(line, "STATUS")) {
        print_status();
        return;
    }
    if (!strcmp(line, "ALL_OFF")) {
        broadcast_all_off();
        if (s_uart_debug) {
            print_status();
        }
        return;
    }
    if (!strcmp(line, "DISCOVER")) {
        broadcast_discover();
        collect_reports_ms(500);
        return;
    }
    if (!strcmp(line, "SYNC")) {
        broadcast_sync();
        return;
    }
    if (!strcmp(line, "START")) {
        broadcast_system_ctrl(SYS_CMD_START);
        return;
    }
    if (!strcmp(line, "STOP")) {
        broadcast_system_ctrl(SYS_CMD_STOP);
        return;
    }
    if (!strcmp(line, "PAUSE")) {
        broadcast_system_ctrl(SYS_CMD_PAUSE);
        return;
    }

    int ids[MAGNET_COUNT];
    int count;
    if (!strncmp(line, "ON ", 3)) {
        count = parse_key_list(line + 3, ids, MAGNET_COUNT);
        if (count < 0) {
            return;
        }
        for (int i = 0; i < count; i++) {
            s_key_state |= (1u << (ids[i] - 1));
        }
        mark_dirty();
        return;
    }
    if (!strncmp(line, "OFF ", 4)) {
        count = parse_key_list(line + 4, ids, MAGNET_COUNT);
        if (count < 0) {
            return;
        }
        for (int i = 0; i < count; i++) {
            s_key_state &= ~(1u << (ids[i] - 1));
        }
        mark_dirty();
        return;
    }
    if (!strncmp(line, "SET ", 4)) {
        count = parse_key_list(line + 4, ids, MAGNET_COUNT);
        if (count < 0) {
            return;
        }
        s_key_state = 0;
        for (int i = 0; i < count; i++) {
            s_key_state |= (1u << (ids[i] - 1));
        }
        mark_dirty();
        flush_if_due(true);
        return;
    }
    uart_report("ERR unknown, type HELP\r\n");
}

void app_main(void)
{
    if (can_init() != ESP_OK || cmd_uart_init() != ESP_OK) {
        return;
    }
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("READY DEBUG=%d BITMAP=1\r\n", s_uart_debug ? 1 : 0);

    broadcast_discover();
    collect_reports_ms(400);
    broadcast_sync();

    char line[160];
    while (1) {
        can_ensure_running();
        int n = cmd_uart_read_line(line, sizeof(line), 5);
        if (n > 0) {
            handle_line(line);
        }
        flush_if_due(false);
    }
}
