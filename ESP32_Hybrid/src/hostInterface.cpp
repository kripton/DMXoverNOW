#include "hostInterface.h"

hostInterface::hostInterface(/* args */)
{
    // Set up our serial port for communication with the HOST
    uart_param_config(UART_NUM_0, &uartMasteronfig);
    uart_driver_install(UART_NUM_0, BUF_SIZE * 2, BUF_SIZE * 2, 10, &uart0Queue, 0);
    uart_enable_pattern_det_intr(UART_NUM_0, 0, 1, 10000, 10, 10);
    xTaskCreate(uartMasterTask, "uartMasterTask", 2048, NULL, 12, NULL);
}

hostInterface::~hostInterface()
{
}

void hostInterface::encodeAndSendReplyToHost(size_t length_in) {
  size_t sendSize = 0;

  // Encode the data to be sent as Bse64
  base64_init_encodestate(&::b64enc);
  sendSize += base64_encode_block((const char*)serialInDecoded, length_in, (char*)serialInData, &b64enc);
  sendSize += base64_encode_blockend((char*)(serialInData + sendSize), &b64enc);

  // Terminate it with a NULL-byte
  sendSize++;
  serialInData[sendSize] = 0;

  // And send it
  uart_write_bytes(UART_NUM_0, (const char*)serialInData, sendSize);
}

void hostInterface::handleSerialData(size_t readLength) {
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

void hostInterface::uartMasterTask (void* pvParameters) {
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
          hostInterface::handleSerialData(readLength);
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

void hostInterface::cmd_init() {
  size_t offset = 0;
  
  // Clear all buffers
  memset(dmxBuf, 0, 512 * DMX_UNIVERSES);
  memset(dmxPrevBuf, 0, 512 * DMX_UNIVERSES);
  memset(dmxCompBuf, 0, 600);
  spinner = 0;
  spinCount = 0;
  dmxCompSize = 0;

  // Read host name and save it
  memset((void*)line1.c_str(), 0, 25);
  sprintf((char*)line1.c_str(), "H: ");
  memcpy((void*)line1.c_str() + 3, serialInDecoded + 2, 16);

  // Reply
  memset((void*)serialInDecoded + offset, 0x81, 1);                        // Reply to 0x01 command
  offset += 1;
  memset((void*)serialInDecoded + offset, 0, 1);                           // 0x00 = all good
  offset += 1;
  memset((void*)serialInDecoded + offset, 0, 1);                           // Protocol version
  offset += 1;
  sprintf((char*)serialInDecoded + offset, "00.00.00");                    // FW version
  offset += 8;
  memset((void*)serialInDecoded + offset, persistentData.nowChannel, 1);   // NOW channel
  offset += 1;
  //memcpy((void*)serialInDecoded + offset, persistentData.nowKey, 16);      // NOW master key
  offset += 16;
  memcpy((void*)serialInDecoded + offset, &chipid, 8);                     // Unique device ID
  offset += 8;
  memcpy((void*)serialInDecoded + offset, persistentData.networkName, 16); // Network name
  offset += 16;
  
  encodeAndSendReplyToHost(offset);
}

void hostInterface::cmd_ping() {
  size_t offset = 0;

  // Reply
  memset((void*)serialInDecoded + offset, 0x82, 1);                 // Reply to 0x02 command
  offset += 1;
  memset((void*)serialInDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void hostInterface::cmd_confNow() {
  size_t offset = 0;

  // Save the new values
  persistentData.nowChannel = *(serialInDecoded + 1);
  //memcpy(persistentData.nowKey, serialInDecoded + 2, 16);
  EEPROM.put(0, persistentData);
  EEPROM.commit();

  // TODO: Check return values and signal EEPROM errors to Host?

  // Reply
  memset((void*)serialInDecoded + offset, 0x83, 1);                 // Reply to 0x03 command
  offset += 1;
  memset((void*)serialInDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void hostInterface::cmd_confName() {
  size_t offset = 0;

  // Save the new values
  memcpy(persistentData.networkName, serialInDecoded + 1, 16);
  EEPROM.put(0, persistentData);
  EEPROM.commit();

  // TODO: Check return values and signal EEPROM errors to Host?

  // Reply
  memset((void*)serialInDecoded + offset, 0x84, 1);                 // Reply to 0x04 command
  offset += 1;
  memset((void*)serialInDecoded + offset, 0, 1);                    // 0x00 = all good
  offset += 1;
  
  encodeAndSendReplyToHost(offset);
}

void hostInterface::cmd_startScan() {
  // TODO! For the moment, report unsupported command
  memset((void*)serialInDecoded + 0, 0xff, 1);
  memset((void*)serialInDecoded + 1, 0xff, 1);
  encodeAndSendReplyToHost(2);
}

void hostInterface::cmd_reportScan() {
  // TODO! For the moment, report unsupported command
  memset((void*)serialInDecoded + 0, 0xff, 1);
  memset((void*)serialInDecoded + 1, 0xff, 1);
  encodeAndSendReplyToHost(2);
}

void hostInterface::cmd_setDmx() {
  uint8_t universeId = 0;

  memset((void*)serialInDecoded + 0, 0xa1, 1);                       // Reply to 0x21 command
  
  universeId = *(serialInDecoded + 1);
  if (universeId >= DMX_UNIVERSES) {
    // Out of range
    memset((void*)serialInDecoded + 1, 0x81, 1);                     // Signal an error
    encodeAndSendReplyToHost(2);
    return;
  }

  // Copy the current frame to the previous frame data
  memcpy(dmxPrevBuf[universeId], dmxBuf[universeId], 512);

  // Copy the new data to the current frame
  memcpy(dmxBuf[universeId], serialInDecoded + 2, 512);

  memset((void*)serialInDecoded + 1, 0x00, 1);                       // All okay
  encodeAndSendReplyToHost(2);

  sendDmx(universeId);
}