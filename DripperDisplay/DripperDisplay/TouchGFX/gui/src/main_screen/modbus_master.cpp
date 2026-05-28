// modbus_master.cpp — STM32 HMI  Modbus RTU master
// Assumes:
//   - UART1 configured for 19200 8E1 (even parity, 1 stop bit)
//   - One GPIO for the RS-485 DE/RE direction pin
//   - HAL_UART_Transmit / HAL_UART_Receive blocking calls

#include "modbus_master.hpp"
#include "stm32u5xx_hal.h"
#include <string.h>

// ── Hardware config — adjust to your board ────────────────
extern UART_HandleTypeDef huart1;

// RS-485 direction pin: set HIGH before TX, LOW after TX.
// Change port/pin to whatever your board uses.
#define MB_DE_PORT   GPIOA
#define MB_DE_PIN    GPIO_PIN_8

// Timeouts (ms)
#define MB_TX_TIMEOUT_MS   50
#define MB_RX_TIMEOUT_MS  150

// ── CRC-16/IBM (Modbus standard) ──────────────────────────
static uint16_t crc16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFu;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x0001u) crc = (crc >> 1) ^ 0xA001u;
            else               crc >>= 1;
        }
    }
    return crc;
}

// ── RS-485 direction helpers ───────────────────────────────
static inline void de_tx(void)
{
    HAL_GPIO_WritePin(MB_DE_PORT, MB_DE_PIN, GPIO_PIN_SET);
    // Small setup time — adjust for your transceiver (MAX485 needs ~0 ns
    // but a couple of microseconds of margin never hurts)
}

static inline void de_rx(void)
{
    // Wait for the last byte to fully clock out before releasing DE.
    // HAL_UART_Transmit is blocking so by the time it returns, the
    // UART shift register is empty — one bit-time of margin is enough.
    // At 19200 baud, 1 bit ≈ 52 µs. HAL_Delay(1) is 1 ms — safe.
    HAL_Delay(1);
    HAL_GPIO_WritePin(MB_DE_PORT, MB_DE_PIN, GPIO_PIN_RESET);
}

// ── Internal: send frame, receive response ─────────────────
// frame    — bytes to send (CRC already appended)
// frame_len — total frame length including CRC
// resp     — buffer for response
// resp_len  — expected response length
// Returns true if response received and slave ID / function code match.
static bool mb_transaction(const uint8_t *frame, uint8_t frame_len,
                            uint8_t *resp, uint8_t resp_len)
{
    de_tx();
    HAL_UART_Transmit(&huart1, (uint8_t *)frame, frame_len, MB_TX_TIMEOUT_MS);
    de_rx();

    memset(resp, 0, resp_len);
    HAL_StatusTypeDef st = HAL_UART_Receive(&huart1, resp, resp_len,
                                             MB_RX_TIMEOUT_MS);
    if (st != HAL_OK) return false;

    // Minimal sanity: slave ID and function code must echo back
    if (resp[0] != MB_SLAVE_ID) return false;
    if (resp[1] != frame[1])    return false;   // FC must match (not an exception)

    // Verify response CRC
    uint16_t rcrc = crc16(resp, resp_len - 2);
    uint16_t gcrc = (uint16_t)resp[resp_len - 2]
                  | ((uint16_t)resp[resp_len - 1] << 8);
    return (rcrc == gcrc);
}

// ── Build an 8-byte Modbus frame (FC06 or FC04) ───────────
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
    frame[6] = crc & 0xFFu;        // CRC low byte first (Modbus spec)
    frame[7] = (crc >> 8) & 0xFFu;
}

// ── Public API ─────────────────────────────────────────────

void mb_master_init(void)
{
    // DE pin starts LOW (receive mode)
    HAL_GPIO_WritePin(MB_DE_PORT, MB_DE_PIN, GPIO_PIN_RESET);
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

    // Clear prime command after sending so it doesn't latch
    if (ok) {
        build_frame(frame, 0x06, MB_HOLD_MODE, MB_MODE_NORMAL);
        mb_transaction(frame, 8, resp, 8);
    }
    return ok;
}

bool mb_set_drip_rate(float dpm)
{
    if (dpm < 5.0f || dpm > 50.0f) return false;

    uint8_t frame[8], resp[8];
    uint16_t val = (uint16_t)(dpm * 10.0f + 0.5f);  // round to nearest
    build_frame(frame, 0x06, MB_HOLD_DRIP_RATE_SP, val);
    return mb_transaction(frame, 8, resp, 8);
}

bool mb_poll(MB_PollResult *out)
{
    if (!out) return false;

    // FC04: read 4 input registers starting at address 0
    // Request: [id, 0x04, 0x00, 0x00, 0x00, 0x04, CRC_lo, CRC_hi]
    // Response: [id, 0x04, byte_count=8, reg0_hi, reg0_lo, ... reg3_hi, reg3_lo, CRC_lo, CRC_hi]
    const uint8_t COUNT = 4u;
    const uint8_t RESP_LEN = 3u + COUNT * 2u + 2u;  // = 13 bytes

    uint8_t frame[8];
    uint8_t resp[13] = {0};

    build_frame(frame, 0x04, 0u, COUNT);

    if (!mb_transaction(frame, 8, resp, RESP_LEN)) return false;

    // byte_count field should equal COUNT*2
    if (resp[2] != COUNT * 2u) return false;

    // Parse big-endian register values
    out->status       = ((uint16_t)resp[3]  << 8) | resp[4];
    out->drip_rate    = ((uint16_t)resp[5]  << 8) | resp[6];
    out->drip_rate_sp = ((uint16_t)resp[7]  << 8) | resp[8];
    out->alarms       = ((uint16_t)resp[9]  << 8) | resp[10];

    return true;
}