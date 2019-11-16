#include "radiointerface.h"

// Stores one complete dmx universe in compressed form
uint8_t radiointerface::dmxCompBuf[600];
size_t  radiointerface::dmxCompSize = 0;

radiointerface::radiointerface()
{
    // Stores one complete dmx universe in compressed form
    memset(radiointerface::dmxCompBuf, 0, 600);
    radiointerface::dmxCompSize = 0;
}

radiointerface::~radiointerface()
{
}
