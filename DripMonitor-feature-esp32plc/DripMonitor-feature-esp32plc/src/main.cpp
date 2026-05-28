// *************************************************************
// Includes
// *************************************************************
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <cstring>
#include <cstdlib>

#include "storage.hpp"

LOG_MODULE_REGISTER(app_main, CONFIG_APP_MAIN_LOG_LEVEL);

// *************************************************************
// Constants
// *************************************************************
#define SERIAL_MAX_LEN 32
#define API_KEY_MAX_LEN 64
#define INPUT_TIMEOUT_MS 30000
#define PROVISIONING_TIMEOUT_MS 5000
#define UART_MSG_QUEUE_SIZE 256

// *************************************************************
// Global Variables
// *************************************************************
bool provisioning_mode = true;  // Start in provisioning mode, prevents other threads from running
static const struct device *uart_dev;

K_MSGQ_DEFINE(uart_msgq, sizeof(char), UART_MSG_QUEUE_SIZE, 1);

// *************************************************************
// UART Functions
// *************************************************************
static void uart_irq_callback(const struct device *dev, void *user_data)
{
    uint8_t c;
    
    if (!uart_irq_update(dev)) {
        return;
    }

    while (uart_irq_rx_ready(dev)) {
        if (uart_fifo_read(dev, &c, 1) == 1) {
            k_msgq_put(&uart_msgq, &c, K_NO_WAIT);
        }
    }
}

static void uart_print(const char *str) {
    if (!str) return;
    
    while (*str) {
        uart_poll_out(uart_dev, *str++);
    }
}

static int uart_read_line(char *buffer, size_t max_len, int timeout_ms) {
    if (!buffer || max_len == 0) {
        return -1;
    }
    
    size_t pos = 0;
    int64_t start_time = k_uptime_get();
    char c;
    
    while (pos < max_len - 1) {
        if (k_msgq_get(&uart_msgq, &c, K_MSEC(10)) == 0) {
            if (c == '\r' || c == '\n') {
                buffer[pos] = '\0';
                uart_print("\r\n");
                return pos;
            } else if (c == '\b' || c == 127) { // Backspace
                if (pos > 0) {
                    pos--;
                    uart_print("\b \b");  // Erase character
                }
            } else if (c >= 32 && c <= 126) { // Printable characters
                buffer[pos++] = c;
                uart_poll_out(uart_dev, c);  // Echo
            }
        } else {
            // Check timeout
            if (timeout_ms > 0 && (k_uptime_get() - start_time) > timeout_ms) {
                return -1;
            }
        }
    }
    
    buffer[pos] = '\0';
    return pos;
}

// *************************************************************
// Helper Functions
// *************************************************************
static void obscure_api_key(const char *api_key, char *obscured, size_t obscured_size) {
    size_t len = strlen(api_key);
    
    if (len <= 5 || obscured_size <= len) {
        // If key is short or buffer too small, copy as-is
        strncpy(obscured, api_key, obscured_size - 1);
        obscured[obscured_size - 1] = '\0';
        return;
    }
    
    strcpy(obscured, api_key);
    // Obscure all but last 5 characters, preserve hyphens
    for (size_t i = 0; i < len - 5; i++) {
        if (obscured[i] != '-') {
            obscured[i] = '*';
        }
    }
}

static bool validate_input_length(const char *input, size_t max_len, const char *field_name) {
    size_t len = strlen(input);
    if (len == 0) {
        uart_print("Error: ");
        uart_print(field_name);
        uart_print(" cannot be empty!\r\n");
        return false;
    }
    if (len >= max_len) {
        uart_print("Error: ");
        uart_print(field_name);
        uart_print(" too long!\r\n");
        return false;
    }
    return true;
}

static void clear_input_queue() {
    char c;
    while (k_msgq_get(&uart_msgq, &c, K_NO_WAIT) == 0) {
        // Consume any remaining characters
    }
}

// *************************************************************
// Provisioning Functions
// *************************************************************
static void show_current_values() {
    char serial[SERIAL_MAX_LEN] = {0};
    char apikey[API_KEY_MAX_LEN] = {0};
    char obscured[API_KEY_MAX_LEN] = {0};
    
    uart_print("\r\n=== Current Stored Values ===\r\n");
    
    // Show Serial Number
    if (storageRead(FS_SERIAL_NUM, serial, sizeof(serial)) > 0) {
        uart_print("Serial Number: ");
        uart_print(serial);
        uart_print("\r\n");
    } else {
        uart_print("Serial Number: [Not Set]\r\n");
    }
    
    // Show API Key (obscured)
    if (storageRead(FS_API_KEY, apikey, sizeof(apikey)) > 0) {
        uart_print("API Key: ");
        obscure_api_key(apikey, obscured, sizeof(obscured));
        uart_print(obscured);
        uart_print("\r\n");
    } else {
        uart_print("API Key: [Not Set]\r\n");
    }
    
    uart_print("\r\n");
}

static void set_serial_number() {
    char inp_serial[SERIAL_MAX_LEN];
    
    uart_print("Enter Serial Number (max 31 chars): ");
    if (uart_read_line(inp_serial, sizeof(inp_serial), INPUT_TIMEOUT_MS) > 0) {
        if (validate_input_length(inp_serial, SERIAL_MAX_LEN, "Serial Number")) {
            if (storageWrite(FS_SERIAL_NUM, inp_serial, sizeof(inp_serial)) >= 0) {
                uart_print("Serial Number saved successfully!\r\n");
            } else {
                uart_print("Error saving Serial Number!\r\n");
            }
        }
    }
}

static void set_api_key() {
    char inp_apikey[API_KEY_MAX_LEN];
    
    uart_print("Enter API Key (max 63 chars): ");
    if (uart_read_line(inp_apikey, sizeof(inp_apikey), INPUT_TIMEOUT_MS) > 0) {
        if (validate_input_length(inp_apikey, API_KEY_MAX_LEN, "API Key")) {
            if (storageWrite(FS_API_KEY, inp_apikey, sizeof(inp_apikey)) >= 0) {
                uart_print("API Key saved successfully!\r\n");
            } else {
                uart_print("Error saving API Key!\r\n");
            }
        }
    }
}

static void clear_all_storage() {
    char confirmation[4];
    
    uart_print("WARNING: This will clear ALL stored data!\r\n");
    uart_print("Are you sure? (y/N): ");
    
    if (uart_read_line(confirmation, sizeof(confirmation), INPUT_TIMEOUT_MS) > 0) {
        if (confirmation[0] == 'y' || confirmation[0] == 'Y') {
            if (storageInit(true) == 0) {
                uart_print("Storage cleared successfully!\r\n");
                if (storageInit(false) < 0) {
                    uart_print("Warning: Error re-initializing storage!\r\n");
                }
            } else {
                uart_print("Error clearing storage!\r\n");
            }
        } else {
            uart_print("Operation cancelled.\r\n");
        }
    }
}

static void provisioning_menu() {
    char inp_menu[4];
    int choice;
    
    while (1) {
        uart_print("\r\n=== Device Provisioning Menu ===\r\n");
        uart_print("1. Set Serial Number\r\n");
        uart_print("2. Set API Key\r\n");
        uart_print("3. Show Current Values\r\n");
        uart_print("4. Clear All Storage\r\n");
        uart_print("5. Exit\r\n");
        uart_print("\r\nEnter choice (1-5): ");
        
        if (uart_read_line(inp_menu, sizeof(inp_menu), INPUT_TIMEOUT_MS) <= 0) {
            uart_print("Timeout or error. Exiting...\r\n");
            break;
        }
        
        choice = atoi(inp_menu);
        
        switch (choice) {
            case 1:
                set_serial_number();
                break;
            case 2:
                set_api_key();
                break;
            case 3:
                show_current_values();
                break;
            case 4:
                clear_all_storage();
                break;
            case 5:
                uart_print("Exiting provisioning menu...\r\n");
                return;
            default:
                uart_print("Invalid choice. Please enter 1-5.\r\n");
                break;
        }
    }
}

static bool check_for_provisioning_request() {
    uart_print("\r\nPress Enter within 5 seconds for provisioning menu...\r\n");
    
    int64_t start_time = k_uptime_get();
    char c;
    
    while (k_uptime_get() - start_time < PROVISIONING_TIMEOUT_MS) {
        if (k_msgq_get(&uart_msgq, &c, K_MSEC(10)) == 0) {
            uart_print("Key detected! Entering provisioning menu...\r\n");
            clear_input_queue();
            return true;
        }
    }
    
    uart_print("Timeout. Continuing with normal operation...\r\n");
    return false;
}

// *************************************************************
// Main Function
// *************************************************************
int main() {
    // Initialize UART
    uart_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    // Setup interrupt-driven UART
    uart_irq_callback_set(uart_dev, uart_irq_callback);
    uart_irq_rx_enable(uart_dev);

    // Initialize storage
    if (storageInit(false) < 0) {
        LOG_ERR("Storage initialization failed");
        return -1;
    }
    
    // Check for provisioning request
    if (check_for_provisioning_request()) {
        provisioning_menu();
    }
    
    // Provisioning complete - allow other threads to start
    provisioning_mode = false;
    LOG_INF("Provisioning complete. Starting normal operation...");

    while (1){
        LOG_INF("Main loop");
        k_sleep(K_MSEC(60000));
    }

    return 0;
}
