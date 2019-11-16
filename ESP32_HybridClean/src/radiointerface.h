#ifndef RADIOINTERFACE_H
#define RADIOINTERFACE_H

// WiFI and ESP-NOW
#include <esp_now.h>
#include <WiFi.h>

// Data compression and decompression
#include "heatshrink_encoder.h"
#include "heatshrink_decoder.h"

#include "mainapp.h"

class radiointerface
{
private:
    // Stores one complete dmx universe in compressed form
    static uint8_t dmxCompBuf[600];
    static size_t  dmxCompSize;

public:
    radiointerface(/* args */);
    ~radiointerface();
};

#endif // RADIOINTERFACE_H
