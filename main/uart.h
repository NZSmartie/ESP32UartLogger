#ifndef UART_H__
#define UART_H__


typedef void(*uart_data_callback_t)(const unsigned char* data, int len);

void uart_init(uart_data_callback_t callback);


#endif /* UART_H__ */