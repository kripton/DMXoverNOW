// ========================================
// ===== INCLUDES =========================
// ========================================

// OLEDD display driver
#include <U8x8lib.h>
#include <U8g2lib.h>

// System stuff (unique Id, reboot reason, ...)
#include <esp_system.h>

// Persistent data storage
#include <EEPROM.h>

// Base64
#include "cdecode.c"
#include "cencode.c"

// Data compression
#include "heatshrink_encoder.c"

// Number of supported DMX universes
#define DMX_UNIVERSES 4


// ========================================
// ===== GLOBALS ==========================
// ========================================

// Unique device identification
uint64_t chipid;
static char deviceid[21];

// Persistent data from and to EEPROM, including defaults
struct PersistentData {
  uint32_t magic = 0xdeadbeef;
  uint8_t nowChannel = 11;
  uint8_t nowKey[16] = {39, 21, 129, 255, 12, 43, 87, 154, 143, 30, 88, 72, 11, 189, 232, 40};
  char networkName[20] = "DMXoverNOW 00      ";
} persistentData, defaultData;

// Serial Input + Output buffer
static uint8_t serialData[1024];
static uint8_t serialDecoded[770];

// Stores the latest values of all universes
static uint8_t dmxBuf[DMX_UNIVERSES][512];
// Stores the previous values of all universes for diff calculation
static uint8_t dmxPrevBuf[DMX_UNIVERSES][512];

// Stores one complete dmx universe in compressed form
static uint8_t dmxCompBuf[600];
static size_t  dmxCompSize = 0;

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
  // Set up the OLED and memory
  u8g2.initDisplay();
  u8g2.setPowerSave(0);
  line1.reserve(25);
  line2.reserve(25);
  line3.reserve(25);
  line4.reserve(25);
  line5.reserve(25);
  line6.reserve(25);

  sprintf((char*)line1.c_str(), "Loading ...       %02d", esp_reset_reason());
  printLCD();

  // Device init
  chipid = ESP.getEfuseMac();
  sprintf(deviceid, "%" PRIu64, chipid);

  //Initialize serial and wait for port to open
  Serial.begin(921600);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  // 10ms should be about 1152 byte at 921600 bit/s
  Serial.setTimeout(10);

  // Read persistent settings (NOW channel, NOW key and network name)
  EEPROM.begin(sizeof(PersistentData));
  // Read the EEPROM to one of the two struct instances
  EEPROM.get(0, persistentData);
  if (persistentData.magic != 0xdeadbeef) {
    // EEPROM data is invalid, write the defaults
    memcpy((void*)&persistentData, (const void*)&defaultData, sizeof(PersistentData));
    EEPROM.put(0, persistentData);
    EEPROM.commit();
  }

  sprintf((char*)line1.c_str(), "Awaiting Init ...  %02d", esp_reset_reason());
  printLCD();
}


// ========================================
// ===== Helper functions==================
// ========================================

void compressDmxBuf(uint8_t universeId) {
  int sink_res = 0;
  size_t sizeSunk = 0;
  int poll_res = 0;

  // Init encoder and zero output buffer
  heatshrink_encoder_reset(&hse);
  memset(dmxCompBuf, 0, 600);
  dmxCompSize = 0;

  // Compresssion
  sink_res = heatshrink_encoder_sink(&hse, (uint8_t*)dmxBuf[universeId], 512, &sizeSunk);
  heatshrink_encoder_finish(&hse);
  
  poll_res = heatshrink_encoder_poll(&hse, (uint8_t*)dmxCompBuf, 600, &dmxCompSize);
}

char getSpinner() {
  switch (spinner % 4) {
    case 1: return '\\'; break;
    case 2: return '|'; break;
    case 3: return '/'; break;
    default: return '-'; break;
  }
}

void printLCD() {
  sprintf((char*)line2.c_str(), "N: %s", persistentData.networkName);
  sprintf((char*)line3.c_str(), "Commands: %05x     %c", commandCount, getSpinner());


  sprintf((char*)line6.c_str(), "%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x", dmxBuf[0][0], dmxBuf[0][1], dmxBuf[0][2], dmxBuf[0][3], dmxBuf[0][4], dmxBuf[0][5], dmxBuf[0][6], dmxBuf[0][7], dmxBuf[0][8], dmxBuf[0][9]);

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
  sendSize += base64_encode_blockend((char*)(serialData + sendSize), &b64enc);
  sendSize++;
  serialData[sendSize] = 0; // Terminate with a NULL-byte
  Serial.write(serialData, sendSize);
  Serial.flush();
}


// ========================================
// ===== Command Handlers =================
// ========================================

void cmd_init() {
  size_t offset = 0;
  
  // Clear all buffers
  memset(dmxBuf, 0, 512 * DMX_UNIVERSES);
  memset(dmxPrevBuf, 0, 512 * DMX_UNIVERSES);
  memset(dmxCompBuf, 0, 600);
  spinner = 0;
  commandCount = 0;
  dmxCompSize = 0;

  // Read host name and save it
  memset((void*)line1.c_str(), 0, 25);
  memcpy((void*)line1.c_str(), serialDecoded + 2, 20);

  // Reply
  memset((void*)serialDecoded + offset, 0x81, 1);                        // Reply to 0x01 command
  offset += 1;
  memset((void*)serialDecoded + offset, 0, 1);                           // 0x00 = all good
  offset += 1;
  memset((void*)serialDecoded + offset, 0, 1);                           // Protocol version
  offset += 1;
  sprintf((char*)serialDecoded + offset, "00.00.00");                    // FW version
  offset += 8;
  memset((void*)serialDecoded + offset, persistentData.nowChannel, 1);   // NOW channel
  offset += 1;
  memcpy((void*)serialDecoded + offset, persistentData.nowKey, 16);      // NOW master key
  offset += 16;
  memcpy((void*)serialDecoded + offset, &chipid, 8);                     // Unique device ID
  offset += 8;
  memcpy((void*)serialDecoded + offset, persistentData.networkName, 20); // Network name
  offset += 20;
  
  encodeAndSendReplyToHost(offset);
}

void cmd_ping() {
  size_t offset = 0;

  // Reply
  memset((void*)serialDecoded + offset, 0x82, 1);                 // Reply to 0x02 command
  offset += 1;
  memset((void*)serialDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void cmd_confNow() {
  size_t offset = 0;

  // Save the new values
  persistentData.nowChannel = *(serialDecoded + 1);
  memcpy(persistentData.nowKey, serialDecoded + 2, 16);
  EEPROM.put(0, persistentData);
  EEPROM.commit();

  // TODO: Check return values and signal EEPROM errors to Host?

  // Reply
  memset((void*)serialDecoded + offset, 0x83, 1);                 // Reply to 0x03 command
  offset += 1;
  memset((void*)serialDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void cmd_confName() {
  size_t offset = 0;

  // Save the new values
  memcpy(persistentData.networkName, serialDecoded + 1, 20);
  EEPROM.put(0, persistentData);
  EEPROM.commit();

  // TODO: Check return values and signal EEPROM errors to Host?

  // Reply
  memset((void*)serialDecoded + offset, 0x84, 1);                 // Reply to 0x04 command
  offset += 1;
  memset((void*)serialDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void cmd_startScan() {
  // TODO! For the moment, report unsupported command
  memset((void*)serialDecoded + 0, 0xff, 1);
  memset((void*)serialDecoded + 1, 0xff, 1);
  encodeAndSendReplyToHost(2);
}

void cmd_reportScan() {
  // TODO! For the moment, report unsupported command
  memset((void*)serialDecoded + 0, 0xff, 1);
  memset((void*)serialDecoded + 1, 0xff, 1);
  encodeAndSendReplyToHost(2);
}

void cmd_setDmx() {
  uint8_t universeId = 0;

  memset((void*)serialDecoded + 0, 0xa1, 1);                       // Reply to 0x21 command
  
  universeId = *(serialDecoded + 1);
  if (universeId >= DMX_UNIVERSES) {
    // Out of range
    memset((void*)serialDecoded + 1, 0x81, 1);                     // Signal an error
    encodeAndSendReplyToHost(2);
    return;
  }

  // Copy the current frame to the previous frame data
  memcpy(dmxPrevBuf[universeId], dmxBuf[universeId], 512);

  // Copy the new data to the current frame
  memcpy(dmxBuf[universeId], serialDecoded + 2, 512);

  memset((void*)serialDecoded + 1, 0x00, 1);                       // All okay
  encodeAndSendReplyToHost(2);

  // TODO: Process DMX data and send update to NOW slaves
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
        cmd_init();
        break;

      case 0x02:
        cmd_ping();
        break;  

      case 0x03:
        cmd_confNow();
        break;

      case 0x04:
        cmd_confName();
        break;

      case 0x11:
        cmd_startScan();
        break;

      case 0x12:
        cmd_reportScan();
        break;

      case 0x21:
        cmd_setDmx();
        break;      

      default:
        commandKnown = 0;
        memset((void*)serialDecoded + 0, 0xff, 1);
        memset((void*)serialDecoded + 1, 0xff, 1);
        encodeAndSendReplyToHost(2);
        break;
    }

    if (commandKnown) {
      commandCount++;
    }
  }

  printLCD();
}
