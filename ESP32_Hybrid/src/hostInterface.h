#include "superglobals.h"

// memset
#include <string.h>

// Low-Level serial driver
#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define BUF_SIZE (1024)

class hostInterface
{
private:
    // Serial handlers, buffers and configs
    // UART0 = USB-to-Serial = HOST to MASTER
    static QueueHandle_t uart0Queue;
    static uint8_t serialInData[1024];
    static uint8_t serialInDecoded[770];

    uart_config_t uartMasteronfig = {
        .baud_rate   =   921600,
        .data_bits   =   UART_DATA_8_BITS,
        .parity      =   UART_PARITY_DISABLE,
        .stop_bits   =   UART_STOP_BITS_1,
        .flow_ctrl   =   UART_HW_FLOWCTRL_DISABLE
    };

    static void encodeAndSendReplyToHost(size_t length_in);
    static void handleSerialData(size_t readLength);

    static void uartMasterTask(void* pvParameters);

    static void cmd_init();
    static void cmd_ping();
    static void cmd_confNow();
    static void cmd_confName();
    static void cmd_startScan();
    static void cmd_reportScan();
    static void cmd_setDmx();

public:
    hostInterface(/* args */);
    ~hostInterface();
};
