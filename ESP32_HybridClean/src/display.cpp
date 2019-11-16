#include "display.h"

#include "mainapp.h"

display::display() {
    *u8g2 = U8G2_SSD1306_128X64_NONAME_F_SW_I2C(U8G2_R0, /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

    // Set up the OLED and memory
    u8g2->initDisplay();
    u8g2->setPowerSave(0);
    line1.reserve(25);
    line2.reserve(25);
    line3.reserve(25);
    line4.reserve(25);
    line5.reserve(25);
    line6.reserve(25);

    sprintf((char*)line1.c_str(), "Loading ...        %02d", esp_reset_reason());
}

display::~display() {
}

void display::printLCD() {
    sprintf((char*)line2.c_str(), "N: %s", mainapp::persistentData.networkName);
    sprintf((char*)line3.c_str(), "Frames: %05x       %c", spinCount, getSpinner());

    sprintf((char*)line6.c_str(), "%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x", dmxBuf[0][0], dmxBuf[0][1], dmxBuf[0][2], dmxBuf[0][3], dmxBuf[0][4], dmxBuf[0][5], dmxBuf[0][508], dmxBuf[0][509], dmxBuf[0][510], dmxBuf[0][511]);

    u8g2->clearBuffer();
    u8g2->setFont(u8g2_font_6x10_tf);
    u8g2->drawUTF8(0, 7,line1.c_str());
    u8g2->drawUTF8(0,18,line2.c_str());
    u8g2->drawUTF8(0,29,line3.c_str());
    u8g2->drawUTF8(0,40,line4.c_str());
    u8g2->drawUTF8(0,51,line5.c_str());
    u8g2->drawUTF8(0,62,line6.c_str());
    u8g2->sendBuffer();
}