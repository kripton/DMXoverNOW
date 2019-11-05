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

// Persistent data storage
#include <EEPROM.h>

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

// Stores the latest values of all universes
static uint8_t dmxBuf[DMX_UNIVERSES][512];

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

// Counts the incoming framed from Master
uint16_t frameCounter = 0;

// Statically allocated heatshrink encoder
heatshrink_encoder hse;

// Global copy of slave / peer device 
// for broadcasts the addr needs to be ff:ff:ff:ff:ff:ff
// all devices on the same channel
esp_now_peer_info_t slaves;

static uint8_t broadcast_mac[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

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

  // Set-up ESP-NOW with broadcasting
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  uint8_t baseMac[6];
  esp_read_mac(baseMac, ESP_MAC_WIFI_STA);
  char baseMacChr[18] = {0};
  memset((void*)line5.c_str(), 0, 25);
  sprintf((char*)line5.c_str(), "MAC:%02X:%02X:%02X:%02X:%02X:%02X", baseMac[0], baseMac[1], baseMac[2], baseMac[3], baseMac[4], baseMac[5]);
  
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

  esp_now_register_recv_cb(msg_recv_cb);
  esp_now_register_send_cb(msg_send_cb);

  sprintf((char*)line1.c_str(), "Ready         ...  %02d", esp_reset_reason());
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
  sprintf((char*)line3.c_str(), "Framed: %05x       %c", frameCounter, getSpinner());


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

static void msg_send_cb(const uint8_t* mac, esp_now_send_status_t sendStatus) {
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

static void msg_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  // On the master only needed for SCAN replies
  spinner++;
  frameCounter++;
  memset((void*)line4.c_str(), 0, 25);
  sprintf((char*)line4.c_str(), "LEN:%d CMD:%02x UID:%02x", len, data[0], data[1]);
  memset((void*)line5.c_str(), 0, 25);
  sprintf((char*)line5.c_str(), "RXF:%02X:%02X:%02X:%02X:%02X:%02X", mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  if ((data[0] == 0x11) && (data[1] == 0)) {
    memcpy(dmxBuf[0], data + 2, 100);
  }
}


// ========================================
// ===== Loop =============================
// ========================================

void loop() {
  printLCD();
}
