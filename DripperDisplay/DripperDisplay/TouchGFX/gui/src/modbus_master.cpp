// modbus_master.cpp — STM32 TouchGFX HMI, Modbus RTU master (FC04 / FC06)
#include "modbus_master.hpp"

#ifdef TARGET_STM32

#include "stm32u5xx_hal.h"
#include "usart.h"
#include <string.h>

extern UART_HandleTypeDef huart6;

#define MB_UART              (&huart6)
#define MB_TX_TIMEOUT_MS     50
#define MB_RX_TIMEOUT_MS     200
/* Modbus RTU T3.5 at 19200 ≈ 2 ms; allow transceiver turnaround */
#define MB_TURNAROUND_MS     3

static uint16_t crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001u) {
                crc = (crc >> 1) ^ 0xA001u;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

static void mb_flush_rx(void)
{
    uint8_t discard;
    while (HAL_UART_Receive(MB_UART, &discard, 1, 1) == HAL_OK) {
    }
}

static bool mb_transaction(const uint8_t *frame, uint8_t frame_len,
                           uint8_t *resp, uint8_t resp_max)
{
    mb_flush_rx();

    if (HAL_UART_Transmit(MB_UART, (uint8_t *)frame, frame_len, MB_TX_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    HAL_Delay(MB_TURNAROUND_MS);

    memset(resp, 0, resp_max);
    if (HAL_UART_Receive(MB_UART, resp, 3, MB_RX_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    uint8_t total = 0;
    if (resp[1] & 0x80) {
        total = 5;
    } else if (resp[1] == 0x04) {
        total = (uint8_t)(3 + resp[2] + 2);
    } else if (resp[1] == 0x06) {
        total = 8;
    } else {
        return false;
    }

    if (total > resp_max) {
        return false;
    }
    if (total > 3 &&
        HAL_UART_Receive(MB_UART, resp + 3, total - 3, MB_RX_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    if (resp[0] != MODBUS_NODE_ID) {
        return false;
    }
    if (resp[1] != frame[1]) {
        return false;
    }

    uint16_t rcrc = crc16(resp, total - 2);
    uint16_t gcrc = (uint16_t)resp[total - 2]
                  | ((uint16_t)resp[total - 1] << 8);
    return (rcrc == gcrc);
}

static void build_frame(uint8_t *frame, uint8_t fc, uint16_t addr, uint16_t val_or_count)
{
    frame[0] = MODBUS_NODE_ID;
    frame[1] = fc;
    frame[2] = (addr >> 8) & 0xFFu;
    frame[3] = addr & 0xFFu;
    frame[4] = (val_or_count >> 8) & 0xFFu;
    frame[5] = val_or_count & 0xFFu;
    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFFu;
    frame[7] = (crc >> 8) & 0xFFu;
}

void mb_init(void)
{
    mb_flush_rx();
}

MB_PollResult mb_poll(void)
{
    MB_PollResult out = {};
    const uint8_t COUNT = 6u;
    const uint8_t RESP_LEN = 3u + COUNT * 2u + 2u;

    uint8_t frame[8];
    uint8_t resp[13] = {0};

    build_frame(frame, 0x04, MB_INP_STATUS, COUNT);

    if (!mb_transaction(frame, 8, resp, RESP_LEN)) {
        return out;
    }

    if (resp[2] != COUNT * 2u) {
        return out;
    }

    out.status       = ((uint16_t)resp[3] << 8) | resp[4];
    out.drip_rate    = ((uint16_t)resp[5] << 8) | resp[6];
    out.drip_rate_sp = ((uint16_t)resp[7] << 8) | resp[8];
    out.alarms       = ((uint16_t)resp[9] << 8) | resp[10];
    out.low_drip_shdn_en =
        ((uint16_t)resp[11] << 8) | resp[12];
    out.low_drip_shdn_delay =
        ((uint16_t)resp[13] << 8) | resp[14];
    out.ok           = true;
    return out;
}

bool mb_write_holding_u16(uint16_t addr, uint16_t val)
{
    uint8_t frame[8], resp[8];
    build_frame(frame, 0x06, addr, val);
    return mb_transaction(frame, 8, resp, 8);
}

bool mb_set_run(bool run)
{
    uint8_t frame[8], resp[8];
    build_frame(frame, 0x06, MB_HOLD_RUN, run ? 1u : 0u);
    return mb_transaction(frame, 8, resp, 8);
}

void mb_send_prime(void)
{
    uint8_t frame[8], resp[8];
    build_frame(frame, 0x06, MB_HOLD_MODE, 1u);
    (void)mb_transaction(frame, 8, resp, 8);
}

bool mb_set_drip_rate(float dpm)
{
    if (dpm < 5.0f || dpm > 50.0f) {
        return false;
    }

    uint8_t frame[8], resp[8];
    uint16_t val = (uint16_t)(dpm * 10.0f + 0.5f);
    build_frame(frame, 0x06, MB_HOLD_DRIP_RATE_SP, val);
    return mb_transaction(frame, 8, resp, 8);
}

#else

void mb_init(void) {}

MB_PollResult mb_poll(void)
{
    return {};
}

bool mb_set_run(bool) { return true; }

void mb_send_prime(void) {}

bool mb_set_drip_rate(float) { return true; }

bool mb_write_holding_u16(uint16_t, uint16_t) { return true; }

#endif
