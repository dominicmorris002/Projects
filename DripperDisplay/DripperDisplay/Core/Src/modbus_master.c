// modbus_master.c — STM32 HMI Modbus RTU master (USART6 + hardware RS485 DE on PE4)

#include "modbus_master.h"
#include "usart.h"
#include "stm32u5xx_hal.h"
#include <string.h>

#define MB_TX_TIMEOUT_MS   50
#define MB_RX_TIMEOUT_MS  150

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
    uint8_t dummy;
    while (HAL_UART_Receive(&huart6, &dummy, 1, 1) == HAL_OK) {
    }
}

static bool mb_transaction(const uint8_t *frame, uint8_t frame_len,
                           uint8_t *resp, uint8_t resp_len)
{
    mb_flush_rx();

    if (HAL_UART_Transmit(&huart6, (uint8_t *)frame, frame_len, MB_TX_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    memset(resp, 0, resp_len);
    if (HAL_UART_Receive(&huart6, resp, resp_len, MB_RX_TIMEOUT_MS) != HAL_OK) {
        return false;
    }

    if (resp[0] != MB_SLAVE_ID) {
        return false;
    }
    if (resp[1] != frame[1]) {
        return false;
    }

    uint16_t rcrc = crc16(resp, resp_len - 2);
    uint16_t gcrc = (uint16_t)resp[resp_len - 2]
                  | ((uint16_t)resp[resp_len - 1] << 8);
    return (rcrc == gcrc);
}

static void build_frame(uint8_t *frame, uint8_t fc,
                        uint16_t addr, uint16_t val_or_count)
{
    frame[0] = MB_SLAVE_ID;
    frame[1] = fc;
    frame[2] = (addr >> 8) & 0xFFu;
    frame[3] = addr & 0xFFu;
    frame[4] = (val_or_count >> 8) & 0xFFu;
    frame[5] = val_or_count & 0xFFu;
    uint16_t crc = crc16(frame, 6);
    frame[6] = crc & 0xFFu;
    frame[7] = (crc >> 8) & 0xFFu;
}

void mb_master_init(void)
{
    mb_flush_rx();
}

bool mb_set_run(bool run)
{
    uint8_t frame[8], resp[8];
    build_frame(frame, 0x06, MB_HOLD_RUN, run ? 1u : 0u);
    return mb_transaction(frame, 8, resp, 8);
}

bool mb_send_prime(void)
{
    uint8_t frame[8], resp[8];
    build_frame(frame, 0x06, MB_HOLD_MODE, MB_MODE_PRIME);
    bool ok = mb_transaction(frame, 8, resp, 8);

    if (ok) {
        build_frame(frame, 0x06, MB_HOLD_MODE, MB_MODE_NORMAL);
        mb_transaction(frame, 8, resp, 8);
    }
    return ok;
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

bool mb_poll(MB_PollResult *out)
{
    if (!out) {
        return false;
    }

    const uint8_t COUNT = 4u;
    const uint8_t RESP_LEN = 3u + COUNT * 2u + 2u;

    uint8_t frame[8];
    uint8_t resp[13] = {0};

    build_frame(frame, 0x04, 0u, COUNT);

    if (!mb_transaction(frame, 8, resp, RESP_LEN)) {
        return false;
    }

    if (resp[2] != COUNT * 2u) {
        return false;
    }

    out->status       = ((uint16_t)resp[3]  << 8) | resp[4];
    out->drip_rate    = ((uint16_t)resp[5]  << 8) | resp[6];
    out->drip_rate_sp = ((uint16_t)resp[7]  << 8) | resp[8];
    out->alarms       = ((uint16_t)resp[9]  << 8) | resp[10];

    return true;
}
