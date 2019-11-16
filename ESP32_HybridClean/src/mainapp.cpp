#include "mainapp.h"

// Init static variables
uint8_t mainapp::deviceMode = 0;
uint8_t mainapp::spinner = 0;
uint16_t mainapp::spinCount = 0;

mainapp::mainapp()
{
    // Init static variables
    mainapp::deviceMode = 0;
    mainapp::spinner = 0;
    mainapp::spinCount = 0;
}

mainapp::~mainapp()
{
}
