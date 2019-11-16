#ifndef MAINAPP_H
#define MAINAPP_H

// ========================================
// ===== CONFIG ===========================
// ========================================

// Number of supported DMX universes
#define DMX_UNIVERSES 8

// How many entries our Radio-send-queue will be able to hold
#define SEND_QUEUE_SIZE 4*DMX_UNIVERSES

// On which pins to transmit which signal
#define DMX1PIN 13
#define DMX2PIN 17
#define TRIGGERHELPER 12


// ========================================
// ===== LIBRARIES ========================
// ========================================

// System stuff (basic data types, unique Id, reboot reason, ...)
#include <esp_system.h>

// Main Event queue
#include <Eventually.h>

// Persistent data storage
#include <EEPROM.h>


// ========================================
// ===== DATA TYPES =======================
// ========================================

// Persistent data from and to EEPROM, including defaults
struct PersistentData {
  uint32_t magic = 0xcafeaffe;
  uint8_t eepromFormatVersion = 0x02;
  uint8_t nowChannel = 11;
  char networkName[16] = "DMXoverNOW 00  ";
  uint8_t universeToSend1 = 0;
  uint8_t universeToSend2 = 1;
};


// ========================================
// ===== App components ===================
// ========================================

class hostinterface;
class radiointerface;
class dmxsender;
class display;

class mainapp
{
private:
    
public:
    mainapp();
    ~mainapp();

    // Main loop manager
    EvtManager mgr;

    // Persistent configuration data
    PersistentData persistentData;
    PersistentData defaultData;

    // Unique device identification
    // TODO: MAC ADDRESS!
    uint64_t chipid;
    static char deviceId[21];

    // Device mode
    //  0 = Not decided yet
    //  1 = Master
    //  2 = Slave
    static uint8_t deviceMode;

    // Spinner with 4 possible states
    static uint8_t spinner;

    // Counts the incoming commands from HOST
    // OR the number of incoming radio frames
    static uint16_t spinCount;

    // Stores the latest values of all universes
    static uint8_t dmxBuf[DMX_UNIVERSES][512];
    // Stores the previous values of all universes for diff calculation (needed by master)
    static uint8_t dmxPrevBuf[DMX_UNIVERSES][512];


    // COMPONENTS
    static display* disp;
};

#endif // MAINAPP_H
