#include <U8x8lib.h>
#include <U8g2lib.h>

#include <driver/rtc_io.h>
#include <esp_system.h>

#include <WiFi.h>

#include "cdecode.c"
#include "cencode.c"

#include "heatshrink_encoder.c"

// Unique device identification
uint64_t chipid;
static char deviceid[21];

// Input + Output buffer
static uint8_t serialData[1024];
static uint8_t serialDecoded[770];

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

// Statically allocated Base64 contexts
base64_decodestate b64dec;
base64_encodestate b64enc;

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

void encodeAndSendReplyToHost(size_t length_in) {
  size_t sendSize = 0;
  
  base64_init_encodestate(&b64enc);
  sendSize += base64_encode_block((const char*)serialDecoded, length_in, (char*)serialData, &b64enc);
  sendSize += base64_encode_blockend((char*)serialData, &b64enc);
  sendSize++;
  serialData[sendSize] = 0; // Terminate with a NULL-byte
  Serial.write(serialData, sendSize);
  Serial.flush();
}

// ========================================
// ===== Command Handlers==================
// ========================================

// INIT
void cmd_01() {
  // Clear all buffers
  memset(dmxBuf, 0, 512 * DMX_UNIVERSES);
  memset(dmxPrevBuf, 0, 512 * DMX_UNIVERSES);
  spinner = 0;
  commandCount = 0;

  // Read host name and save it
  memset((void*)line1.c_str(), 0, 25);
  memcpy((void*)line1.c_str(), serialDecoded + 2, 20);

  // Reply
  sprintf((char*)serialDecoded, "00.00.00\x00\x0b\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00MyCoolNetwork       ");
  /*
  Serial.write("00.00.00"); // FW version
  Serial.write(0);          // Protocol version
  Serial.write(11);         // NOW channel
  Serial.write(dmxBuf[0], 16); // NOW Key
  Serial.write(&chipid, 8); // Unique device ID
  Serial.write("MyCoolNetwork       "); // Network name
  */
  encodeAndSendReplyToHost(54);
}

// ========================================
// ===== Loop =============================
// ========================================


void loop() {
  size_t readLength = 0;
  size_t decodedLength = 0;
  int commandKnown = 1;

  readLength = Serial.readBytesUntil('\0', (char*)serialData, 1024);

  if (readLength > 0) {
    spinner++;
    commandKnown = 1;

    // Decode base64
    memset(serialDecoded, 0, 770);
    base64_init_decodestate(&b64dec);
    decodedLength = base64_decode_block((const char*)serialData, readLength, (char*)serialDecoded, &b64dec);
  
    // DEBUG
    memset((void*)line3.c_str(), 0, 25);
    memset((void*)line4.c_str(), 0, 25);
    sprintf((char*)line3.c_str(), "InSz: %d DecSz: %d", readLength, decodedLength);
    sprintf((char*)line4.c_str(), "IN: %02x %02x %02x %02x %02x %02x ", serialDecoded[0], serialDecoded[1], serialDecoded[2], serialDecoded[3], serialDecoded[4], serialDecoded[5]);
    // /DEBUG

    switch (serialDecoded[0]) {
      case 0x01:
        // INIT
        cmd_01();
        break;

      default:
        commandKnown = 0;
        Serial.write(0xff);
        Serial.write(0xff);
        Serial.write(0x00);
        Serial.flush();
        break;
    }

    if (commandKnown) {
      commandCount++;
    }
  }
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
