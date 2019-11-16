#ifndef DMXSENDER_H
#define DMXSENDER_H

// Low-Level serial driver
#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define BUF_SIZE (1024)

class mainapp;

class dmxsender
{
private:
    // UART1 = DMX send 1
    static QueueHandle_t uart1Queue;
    static uint8_t serialDmx1Buffer[512];

    // UART2 = DMX send 2
    static QueueHandle_t uart2Queue;
    static uint8_t serialDmx2Buffer[512];

    uart_config_t uartDmxConfig = {
        .baud_rate   =   250000,
        .data_bits   =   UART_DATA_8_BITS,
        .parity      =   UART_PARITY_DISABLE,
        .stop_bits   =   UART_STOP_BITS_2,
        .flow_ctrl   =   UART_HW_FLOWCTRL_DISABLE
    };

public:
    dmxsender(/* args */);
    ~dmxsender();
};

#endif // DMXSENDER_H
