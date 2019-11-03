// ========================================
// ===== INCLUDES =========================
// ========================================

// OLEDD display driver
#include <U8x8lib.h>
#include <U8g2lib.h>

// System stuff (unique Id, reboot reason, ...)
#include <esp_system.h>

// Low-Level serial driver
#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define EX_UART_NUM UART_NUM_0
#define BUF_SIZE (1024)

// Persistent data storage
#include <EEPROM.h>

// Base64
#include "cdecode.c"
#include "cencode.c"

// Data compression
#include "heatshrink_encoder.c"


// ========================================
// ===== GLOBALS ==========================
// ========================================

// Number of supported DMX universes
#define DMX_UNIVERSES 4

// Unique device identification
uint64_t chipid;
static char deviceid[21];

// Persistent data from and to EEPROM, including defaults
struct PersistentData {
  uint32_t magic = 0xcafeaffe;
  uint8_t nowChannel = 11;
  uint8_t nowKey[16] = {39, 21, 129, 255, 12, 43, 87, 154, 143, 30, 88, 72, 11, 189, 232, 40};
  char networkName[16] = "DMXoverNOW 00  ";
} persistentData, defaultData;

// Serial Input + Output buffer
static QueueHandle_t uart0_queue;
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

  sprintf((char*)line1.c_str(), "Loading ...        %02d", esp_reset_reason());
  printLCD();

  // Device init
  chipid = ESP.getEfuseMac();
  sprintf(deviceid, "%" PRIu64, chipid);

  //Initialize serial and wait for port to open
  uart_config_t uart_config = {
    .baud_rate   =   921600,
    .data_bits   =   UART_DATA_8_BITS,
    .parity      =   UART_PARITY_DISABLE,
    .stop_bits   =   UART_STOP_BITS_1,
    .flow_ctrl   =   UART_HW_FLOWCTRL_DISABLE
  };
  uart_param_config(EX_UART_NUM, &uart_config);
  uart_driver_install(EX_UART_NUM, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart0_queue, 0);
  uart_enable_pattern_det_intr(EX_UART_NUM, 0, 1, 10000, 10, 10);
  xTaskCreate(uart_event_task, "uart_event_task", 2048, NULL, 12, NULL);

  // Read persistent settings (NOW channel, NOW key and network name)
  EEPROM.begin(sizeof(PersistentData));
  // Read the EEPROM to one of the two struct instances
  EEPROM.get(0, persistentData);
  delay(500);
  if (persistentData.magic != defaultData.magic) {
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


  sprintf((char*)line6.c_str(), "%02x%02x%02x%02x%02x%02x %02x%02x%02x%02x", dmxBuf[0][0], dmxBuf[0][1], dmxBuf[0][2], dmxBuf[0][3], dmxBuf[0][4], dmxBuf[0][5], dmxBuf[0][508], dmxBuf[0][509], dmxBuf[0][510], dmxBuf[0][511]);

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

  // Encode the data to be sent as Bse64
  base64_init_encodestate(&b64enc);
  sendSize += base64_encode_block((const char*)serialDecoded, length_in, (char*)serialData, &b64enc);
  sendSize += base64_encode_blockend((char*)(serialData + sendSize), &b64enc);

  // Terminate it with a NULL-byte
  sendSize++;
  serialData[sendSize] = 0;

  // And send it
  uart_write_bytes(UART_NUM_0, (const char*)serialData, sendSize);
}

void handleSerialData(size_t readLength) {
  size_t decodedLength = 0;
  int commandKnown = 1;

  if (readLength > 0) {
    spinner++;
    commandKnown = 1;

    // Decode base64
    memset(serialDecoded, 0, 770);
    base64_init_decodestate(&b64dec);
    decodedLength = base64_decode_block((const char*)serialData, readLength, (char*)serialDecoded, &b64dec);

    // DEBUG
    memset((void*)line4.c_str(), 0, 25);
    memset((void*)line5.c_str(), 0, 25);
    sprintf((char*)line4.c_str(), "InSz: %d DecSz: %d", readLength, decodedLength);
    sprintf((char*)line5.c_str(), "IN: %02x %02x %02x %02x %02x %02x ", serialDecoded[0], serialDecoded[1], serialDecoded[2], serialDecoded[3], serialDecoded[4], serialDecoded[5]);
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
}

static void uart_event_task (void* pvParameters) {
  uart_event_t event;
  size_t readLength = 0;

  while (1) {
    if (xQueueReceive(uart0_queue, (void*) &event, (portTickType)portMAX_DELAY)) {

      switch (event.type) {
        case UART_DATA:
          break;
        case UART_FIFO_OVF:
          //sprintf((char*)line4.c_str(), "UART_FIFO_OVF");
          uart_flush(EX_UART_NUM);
          break;
        case UART_BUFFER_FULL:
          //sprintf((char*)line4.c_str(), "UART_BUFFER_FULL");
          uart_flush(EX_UART_NUM);
          break;
        case UART_BREAK:
          //sprintf((char*)line4.c_str(), "UART_BREAK");
          break;
        case UART_PARITY_ERR:
          //sprintf((char*)line4.c_str(), "UART_PARITY_ERR");
          break;
        case UART_FRAME_ERR:
          //sprintf((char*)line4.c_str(), "UART_FRAME_ERR");
          break;
        case UART_PATTERN_DET:
          readLength = uart_read_bytes(EX_UART_NUM, serialData, BUF_SIZE, 10 / portTICK_RATE_MS);
          //sprintf((char*)line4.c_str(), "UART_PATTERN_DET %d", readLength);
          handleSerialData(readLength);
          break;
        default:
          break;
      }

      spinner++;
    }
  }

  //vTaskDelete(NULL);
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
  sprintf((char*)line1.c_str(), "H: ");
  memcpy((void*)line1.c_str() + 3, serialDecoded + 2, 16);

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
  memcpy((void*)serialDecoded + offset, persistentData.networkName, 16); // Network name
  offset += 16;
  
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
  memcpy(persistentData.networkName, serialDecoded + 1, 16);
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
  printLCD();
}
