#include <U8x8lib.h>
#include <U8g2lib.h>

#include <driver/rtc_io.h>
#include <esp_system.h>

#include <WiFi.h>

//#include "b64/cdecode.h"

#include "heatshrink_encoder.c"

// Unique device identification
uint64_t chipid;
static char deviceid[21];

static uint8_t serialDataIn[1024];

#define DMX_UNIVERSES 4

// Stores the latest values of all universes
static uint8_t dmxBuf[DMX_UNIVERSES][512];
// Stores the previous values of all universes for diff calculation
static uint8_t dmxPrevBuf[DMX_UNIVERSES][512];

// Stores one complete dmx universe in compressed form
static uint8_t dmxCompBuf[600];

// Instance to talk to our OLED
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// Display content
String line1 = String("");
String line2 = String("");
String line3 = String("");
String line4 = String("");
String line5 = String("");
String line6 = String("");

// Spinner with 4 possible states
uint8_t spinner = 0;

// Counts the incoming commands from HOST
uint16_t commandCount = 0;

// Statically allocated heatshrink encoder
heatshrink_encoder hse;

// ========================================
// ===== SETUP ============================
// ========================================
void setup() {
  chipid = ESP.getEfuseMac();
  sprintf(deviceid, "%" PRIu64, chipid);

  //Initialize serial and wait for port to open
  Serial.begin(921600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // 10ms should be about 1152 byte at 921600 bit/s
  Serial.setTimeout(10);

  u8g2.initDisplay();
  u8g2.setPowerSave(0);

  line1.reserve(25);
  line2.reserve(25);
  line3.reserve(25);
  line4.reserve(25);
  line5.reserve(25);
  line6.reserve(25);

  sprintf((char*)line1.c_str(), "Awaiting Init ... %02d", esp_reset_reason());
  printLCD();
}

// ========================================
// ===== Helper functions==================
// ========================================

void compressDmxBuf() {
  heatshrink_encoder_reset(&hse);
  memset(dmxCompBuf, 0, 600);

  // Compresssion
  size_t sizeSunk = 0;
  int sink_res = heatshrink_encoder_sink(&hse, (uint8_t*)dmxBuf, 512, &sizeSunk);
  //line3 = String("Res: ") + String(sink_res) + String(" Sunk: ") + String(sizeSunk);
  heatshrink_encoder_finish(&hse);
  
  size_t polled = 0;
  int poll_res = heatshrink_encoder_poll(&hse, (uint8_t*)dmxCompBuf, 600, &polled);
}

char getSpinner() {
  switch (spinner % 4) {
    case 0: return '-'; break;
    case 1: return '\\'; break;
    case 2: return '|'; break;
    case 3: return '/'; break;
  }
}

void printLCD() {
  sprintf((char*)line2.c_str(), "Commands: %05x    %c", commandCount, getSpinner());


  sprintf((char*)line6.c_str(), "%02x%02x%02x%02x%02x%02x %02x%02x%02x", dmxBuf[0][0], dmxBuf[0][1], dmxBuf[0][2], dmxBuf[0][3], dmxBuf[0][4], dmxBuf[0][5], dmxBuf[0][6], dmxBuf[0][7], dmxBuf[0][8]);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawUTF8(0, 7,line1.c_str());
  u8g2.drawUTF8(0,18,line2.c_str());
  u8g2.drawUTF8(0,29,line3.c_str());
  u8g2.drawUTF8(0,40,line4.c_str());
  u8g2.drawUTF8(0,51,line5.c_str());
  u8g2.drawUTF8(0,62,line6.c_str());
  u8g2.sendBuffer();
}

// ========================================
// ===== Events============================
// ========================================


void serialEvent() {
  spinner++;
  size_t readLength = 0;

  readLength = Serial.readBytesUntil('\0', (char*)serialDataIn, 1024);
}

// ========================================
// ===== Loop =============================
// ========================================


void loop() {
  /*
  if ((available > 0) && (loopCount <= 100)) {
    packetCount++;
    loopCount++;
    memset(dataIn, 0, 8);
    Serial.readBytes(dataIn, 3);

    if (dataIn[0] == 0x43 && dataIn[1] == 0x3f) {
      memset(dmxBuf, 0, 512);
      line1 = String("Running ...");
    } else if (dataIn[0] == 0xe2) {
      dmxBuf[(uint16_t)dataIn[1]] = dataIn[2];
    } else if (dataIn[0] == 0xe3) {
      dmxBuf[(uint16_t)((uint16_t)dataIn[1] + (uint16_t)256)] = dataIn[2];
    }

    //line2.reserve(30);
    //sprintf((char*)line2.c_str(), "%02x%02x%02x%02x%02x%02x%02x%02x", dataIn[0], dataIn[1], dataIn[2], dataIn[3], dataIn[4], dataIn[5], dataIn[6], dataIn[7]);

    available = Serial.available();
  }
  loopCount = 0;

  // Compress the dmxBuffer
  compressDmxBuf();
  */

  printLCD();
}
