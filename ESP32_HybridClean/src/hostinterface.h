#ifndef HOSTINTERFACE_H
#define HOSTINTERFACE_H

// Low-Level serial driver
#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define BUF_SIZE (1024)

// Base64
#include "cdecode.h"
#include "cencode.h"

class mainapp;

class hostinterface
{
private:
    // Serial Input + Output buffer
    static QueueHandle_t uart0_queue;
    static uint8_t serialData[1024];
    static uint8_t serialDecoded[770];

public:
    hostinterface(/* args */);
    ~hostinterface();
};

#endif // HOSTINTERFACE_H
