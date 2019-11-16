// System stuff (unique Id, reboot reason, ...)
#include <esp_system.h>

// Base64
#include "cdecode.c"
#include "cencode.c"

// Spinner with 4 possible states
uint8_t spinner = 0;

// Counts the incoming commands from HOST
// OR the number of incoming radio frames
uint16_t spinCount = 0;

// Statically allocated Base64 contexts
base64_decodestate b64dec;
base64_encodestate b64enc;

// Stores the latest values of all universes
static uint8_t dmxBuf[DMX_UNIVERSES][512];
// Stores the previous values of all universes for diff calculation
static uint8_t dmxPrevBuf[DMX_UNIVERSES][512];