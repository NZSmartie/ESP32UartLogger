#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/uart.h"

#include "uart.h"


#define TXD_PIN 33
#define RXD_PIN 32

#define RX_BUF_SIZE 1024

static uart_data_callback_t _callback = NULL;

static void rx_task()
{
    static const char *RX_TASK_TAG = "RX_TASK";

    bool receiving = false;
    int rx_data_offset = 0;
    uint8_t* rx_data = (uint8_t*) malloc(RX_BUF_SIZE+1);
    
    esp_log_level_set(RX_TASK_TAG, ESP_LOG_INFO);


    while (1) {
        const int ticks_to_wait = receiving ? pdMS_TO_TICKS(15) : pdMS_TO_TICKS(1000);
        const int rx_bytes = uart_read_bytes(UART_NUM_1, &rx_data[rx_data_offset], 1, ticks_to_wait);

        if (rx_bytes > 0) {
            receiving = true;
            rx_data_offset++;
        }
        else if (rx_bytes == 0 && receiving) // Timed out
        {
            _callback(rx_data, rx_data_offset);

            receiving = false;
            rx_data_offset = 0;
        }
    }
    free(rx_data);
}

void uart_init(uart_data_callback_t callback)
{
    if(_callback != NULL)
        return;
    
    _callback = callback;

    const uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Both TX and RX are inverted
    uart_set_line_inverse(UART_NUM_1, UART_INVERSE_RXD | UART_INVERSE_TXD);

    // We won't use a buffer for sending rx_data.
    uart_driver_install(UART_NUM_1, RX_BUF_SIZE * 2, 0, 0, NULL, 0);

    xTaskCreate(rx_task, "uart_rx_task", 1024 * 2, NULL, configMAX_PRIORITIES, NULL);
    // xTaskCreate(tx_task, "uart_tx_task", 1024*2, NULL, configMAX_PRIORITIES-1, NULL);
}