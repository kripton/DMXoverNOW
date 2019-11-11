// ========================================
// ===== INCLUDES =========================
// ========================================

// OLEDD display driver
#include <U8x8lib.h>
#include <U8g2lib.h>

// System stuff (unique Id, reboot reason, ...)
#include <esp_system.h>

// WiFI and ESP-NOW
#include <esp_now.h>
#include <WiFi.h>

// Low-Level serial driver
#include "driver/uart.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#define BUF_SIZE (1024)

// Persistent data storage
#include <EEPROM.h>

// Base64
#include "cdecode.c"
#include "cencode.c"

// Data compression and decompression
#include "heatshrink_encoder.c"
#include "heatshrink_decoder.c"


// ========================================
// ===== GLOBALS ==========================
// ========================================

// Number of supported DMX universes
#define DMX_UNIVERSES 8

// How many entries our Radio-send-queue will be able to hold
#define SEND_QUEUE_SIZE 4*DMX_UNIVERSES

// On which pins to transmit which signal
#define DMX1PIN 13
#define DMX2PIN 17
#define TRIGGERHELPER 12

// Unique device identification
uint64_t chipid;
static char deviceId[21];

// Persistent data from and to EEPROM, including defaults
struct PersistentData {
  uint32_t magic = 0xcafeaffe;
  uint8_t eepromFormatVersion = 0x02;
  uint8_t nowChannel = 11;
  char networkName[16] = "DMXoverNOW 00  ";
  uint8_t universeToSend1 = 0;
  uint8_t universeToSend2 = 1;
} persistentData, defaultData;

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
// OR the number of incoming radio frames
uint16_t spinCount = 0;

// Statically allocated Base64 contexts
base64_decodestate b64dec;
base64_encodestate b64enc;

// Statically allocated heatshrink encoder & decoder
heatshrink_encoder hse;
heatshrink_decoder hsd;

// Global copy of slave / peer device 
// for broadcasts the addr needs to be ff:ff:ff:ff:ff:ff
// all devices on the same channel
esp_now_peer_info_t slaves;

static uint8_t broadcast_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

// One element of our radio send queue
struct SendQueueElem {
  uint8_t toBeSent = 0;
  uint8_t size = 0;
  uint8_t data[250];
};

// Array called sendQueue ;)
SendQueueElem sendQueue[SEND_QUEUE_SIZE];

// Task to have the display refreshed by the second core
TaskHandle_t displayTaskHandle;

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
  sprintf(deviceId, "%" PRIu64, chipid);

  // Set up our serial port for communication with the HOST
  uart_param_config(UART_NUM_0, &uartMasteronfig);
  uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart0Queue, 0);
  uart_enable_pattern_det_intr(UART_NUM_0, 0, 1, 10000, 10, 10);
  xTaskCreate(uartMasterTask, "uartMasterTask", 2048, NULL, 12, NULL);

  // Set up UART1 for DMX sending
  uart_set_pin(UART_NUM_1, DMX1PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_1, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart1Queue, 0);
  uart_param_config(UART_NUM_1, &uartDmxConfig);

  // Set up UART2 for DMX sending
  uart_set_pin(UART_NUM_2, DMX2PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  uart_driver_install(UART_NUM_2, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart2Queue, 0);
  uart_param_config(UART_NUM_2, &uartDmxConfig);

  // Set up trigger output
  pinMode(TRIGGERHELPER, OUTPUT);

  // Read persistent settings (NOW channel, NOW key and network name)
  EEPROM.begin(sizeof(PersistentData));
  // Read the EEPROM to one of the two struct instances
  EEPROM.get(0, persistentData);
  delay(500);
  if (
      (persistentData.magic != defaultData.magic) ||
      (persistentData.eepromFormatVersion != defaultData.eepromFormatVersion)
     ) {
    // EEPROM data is invalid, write the defaults
    memcpy((void*)&persistentData, (const void*)&defaultData, sizeof(PersistentData));
    EEPROM.put(0, persistentData);
    EEPROM.commit();
  }

  // Set-up ESP-NOW with broadcasting
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  char baseMacChr[18] = {0};
  memset((void*)line5.c_str(), 0, 25);
  sprintf((char*)line5.c_str(), "MAC:%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);

  // Properly zero our sendQueue
  memset(sendQueue, 0, SEND_QUEUE_SIZE*sizeof(SendQueueElem));
  
  if (esp_now_init() == ESP_OK) {
    memset((void*)line4.c_str(), 0, 25);
    sprintf((char*)line4.c_str(), "ESP-NOW Init OK");
  } else {
    memset((void*)line4.c_str(), 0, 25);
    sprintf((char*)line4.c_str(), "ESP-NOW Init Failed");
  }

  slaves.channel = persistentData.nowChannel;
  memset(slaves.peer_addr, 0xff, 6);
  slaves.ifidx = ESP_IF_WIFI_STA;
  slaves.encrypt = false;
  if (esp_now_add_peer(&slaves) == ESP_OK) {
    memset((void*)line4.c_str(), 0, 25);
    sprintf((char*)line4.c_str(), "NOW add_peer OK %d", persistentData.nowChannel);
  } else {
    memset((void*)line4.c_str(), 0, 25);
    sprintf((char*)line4.c_str(), "NOW add_peer Failed");
  }

  esp_now_register_recv_cb(radioRecvCB);
  esp_now_register_send_cb(radioSendCB);

  sprintf((char*)line1.c_str(), "Ready         ...  %02d", esp_reset_reason());

  xTaskCreatePinnedToCore(
      displayTask, /* Function to implement the task */
      "displayTask", /* Name of the task */
      10000,  /* Stack size in words */
      NULL,  /* Task input parameter */
      0,  /* Priority of the task */
      &displayTaskHandle,  /* Task handle. */
      0); /* Core where the task should run */
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

void unCompressDmxBuf(uint8_t universeId) {
  int sink_res = 0;
  size_t sizeSunk = 0;
  int poll_res = 0;

  // Init decoder and zero output buffer
  heatshrink_decoder_reset(&hsd);
  //memset(dmxBuf[universeId], 0, 512);

  sink_res = heatshrink_decoder_sink(&hsd, (uint8_t*)dmxCompBuf, dmxCompSize, &sizeSunk);
  heatshrink_decoder_finish(&hsd);
  size_t polled2 = 0;
  poll_res = heatshrink_decoder_poll(&hsd,(uint8_t*)dmxBuf[universeId], 512, &polled2);
}

char getSpinner() {
  switch (spinner % 4) {
    case 1: return '\\'; break;
    case 2: return '|'; break;
    case 3: return '/'; break;
    default: return '-'; break;
  }
}

void displayTask(void* parameter) {
  for(;;) {
    printLCD();
  }
}

void printLCD() {
  sprintf((char*)line2.c_str(), "N: %s", persistentData.networkName);
  sprintf((char*)line3.c_str(), "Frames: %05x       %c", spinCount, getSpinner());

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
  sendSize += base64_encode_block((const char*)serialInDecoded, length_in, (char*)serialInData, &b64enc);
  sendSize += base64_encode_blockend((char*)(serialInData + sendSize), &b64enc);

  // Terminate it with a NULL-byte
  sendSize++;
  serialInData[sendSize] = 0;

  // And send it
  uart_write_bytes(UART_NUM_0, (const char*)serialInData, sendSize);
}

static void radioSendCB(const uint8_t* mac, esp_now_send_status_t sendStatus) {
  switch (sendStatus)
  {
    case ESP_NOW_SEND_SUCCESS:
      // Send the next packet
      break;

    case ESP_NOW_SEND_FAIL:
      // Empty the sendQueue

      break;

    default:
      break;
  }
}

void handleSerialData(size_t readLength) {
  size_t decodedLength = 0;
  int commandKnown = 1;

  if (readLength > 0) {
    spinner++;
    commandKnown = 1;

    // Decode base64
    memset(serialInDecoded, 0, 770);
    base64_init_decodestate(&b64dec);
    decodedLength = base64_decode_block((const char*)serialInData, readLength, (char*)serialInDecoded, &b64dec);

    //// DEBUG
    //memset((void*)line4.c_str(), 0, 25);
    //memset((void*)line5.c_str(), 0, 25);
    //sprintf((char*)line4.c_str(), "InSz: %d DecSz: %d", readLength, decodedLength);
    //sprintf((char*)line5.c_str(), "IN: %02x %02x %02x %02x %02x %02x ", serialDecoded[0], serialDecoded[1], serialDecoded[2], serialDecoded[3], serialDecoded[4], serialDecoded[5]);
    //// /DEBUG

    switch (serialInDecoded[0]) {
      /*
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
      */

      default:
        commandKnown = 0;
        memset((void*)serialInDecoded + 0, 0xff, 1);
        memset((void*)serialInDecoded + 1, 0xff, 1);
        encodeAndSendReplyToHost(2);
        break;
    }

    if (commandKnown) {
      spinCount++;
    }
  }
}

static void uartMasterTask (void* pvParameters) {
  uart_event_t event;
  size_t readLength = 0;

  while (1) {
    if (xQueueReceive(uart0Queue, (void*) &event, (portTickType)portMAX_DELAY)) {

      switch (event.type) {
        case UART_DATA:
          break;
        case UART_FIFO_OVF:
          //sprintf((char*)line4.c_str(), "UART_FIFO_OVF");
          uart_flush(UART_NUM_0);
          break;
        case UART_BUFFER_FULL:
          //sprintf((char*)line4.c_str(), "UART_BUFFER_FULL");
          uart_flush(UART_NUM_0);
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
          readLength = uart_read_bytes(UART_NUM_0, serialInData, BUF_SIZE, 10 / portTICK_RATE_MS);
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

static void processNextSend() {
  // Iterate through the sendQueue and trigger the first match
  for (int i = 0; i < SEND_QUEUE_SIZE; i++) {
    if (sendQueue[i].toBeSent) {
      esp_now_send(broadcast_mac, sendQueue[i].data, sendQueue[i].size);
      // Zero that element so it won't be sent again
      memset(&(sendQueue[i]), 0, sizeof(SendQueueElem));
      return; // Stop here. Next packet send will be triggered in the send-callback-function
    }
  }
  // If control flow reaches here, the send queue has been emptied
  memset((void*)line5.c_str(), 0, 25);
  sprintf((char*)line4.c_str(), "QUEUE EMPTY");
}

static void radioRecvCB(const uint8_t *mac_addr, const uint8_t *data, int len) {
  // On the master only needed for SCAN replies
  spinner++;
  spinCount++;
  memset((void*)line4.c_str(), 0, 25);
  sprintf((char*)line4.c_str(), "LEN:%d CMD:%02x UID:%02x", len, data[0], data[1]);
  memset((void*)line5.c_str(), 0, 25);
  sprintf((char*)line5.c_str(), "RXF:%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);

  switch (data[0]) {
    case 0x11: // KeyFrame uncompressed 1/3
      if (data[1] == 0) {
        memcpy(dmxBuf[0], data + 2, len - 2);
      }
      break;
    case 0x12: // KeyFrame uncompressed 2/3
      if (data[1] == 0) {
        memcpy(dmxBuf[0] + 171, data + 2, len - 2);
      }
      break;
    case 0x13: // KeyFrame uncompressed 3/3
      if (data[1] == 0) {
        memcpy(dmxBuf[0] + 343, data + 2, len - 2);
      }
      break;
    case 0x14: // KeyFrame compressed 1/1
      memset(dmxCompBuf, 0, 600);
      memcpy(dmxCompBuf, data + 2, len - 2);
      dmxCompSize = len - 2;
      unCompressDmxBuf(data[1]);
      break;
    case 0x15: // KeyFrame compressed 1/2
      memset(dmxCompBuf, 0, 600);
      memcpy(dmxCompBuf, data + 2, len - 2);
      dmxCompSize = len - 2;
      break;
    case 0x16: // KeyFrame compressed 2/2
      memcpy(dmxCompBuf + 230, data + 2, len - 2);
      dmxCompSize += len - 2;
      unCompressDmxBuf(data[1]);
      break;
     case 0x17: // KeyFrame compressed 1/3
      memset(dmxCompBuf, 0, 600);
      memcpy(dmxCompBuf, data + 2, len - 2);
      dmxCompSize = len - 2;
      break;
    case 0x18: // KeyFrame compressed 2/3
      memcpy(dmxCompBuf + 230, data + 2, len - 2);
      dmxCompSize += len - 2;
      break;
    case 0x19: // KeyFrame compressed 3/3
      memcpy(dmxCompBuf + 460, data + 2, len - 2);
      dmxCompSize += len - 2;
      unCompressDmxBuf(data[1]);
      break;
  }
}

void writeDmx(uart_port_t uartId, uint8_t* dmxBuf) {
  uint8_t zero = 0;

  // Send BREAK
  uart_set_line_inverse(uartId, UART_INVERSE_DISABLE);
  delayMicroseconds(200);

  // Send MARK_AFTER_BREAK
  uart_set_line_inverse(uartId, UART_INVERSE_TXD);
  delayMicroseconds(20);

  //send data
  uart_write_bytes(uartId, (const char*)&zero, 1); // start byte
  uart_write_bytes(uartId, (const char*)dmxBuf, 512);
}


// ========================================
// ===== Loop =============================
// ========================================

void loop() {
  digitalWrite(TRIGGERHELPER, HIGH);

  // DMX1
  // Buffer the data to avoid flickering when new data comes in while transmittinh
  memcpy(serialDmx1Buffer, dmxBuf[persistentData.universeToSend1], 512);
  writeDmx(UART_NUM_1, serialDmx1Buffer);

  // DMX2
  // Buffer the data to avoid flickering when new data comes in while transmittinh
  memcpy(serialDmx2Buffer, dmxBuf[persistentData.universeToSend2], 512);
  writeDmx(UART_NUM_2, serialDmx2Buffer);

  digitalWrite(TRIGGERHELPER, LOW);

  delayMicroseconds(25000);
}
