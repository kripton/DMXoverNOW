#ifndef DISPLAY_H
#define DISPLAY_H

// OLED display driver
#include <U8x8lib.h>
#include <U8g2lib.h>

class mainapp;

class display
{
private:
    // Instance to talk to our OLED
    U8G2_SSD1306_128X64_NONAME_F_SW_I2C* u8g2;

    // Display content
    String line1 = String("");
    String line2 = String("");
    String line3 = String("");
    String line4 = String("");
    String line5 = String("");
    String line6 = String("");

    void printLCD();

public:
    display();
    ~display();
};

#endif // DISPLAY_H
