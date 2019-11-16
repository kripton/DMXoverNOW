#include <Arduino.h>

#include "mainapp.h"

#include "display.h"
#include "dmxsender.h"
#include "hostinterface.h"
#include "radiointerface.h"

mainapp app;

void setup() {
  // put your setup code here, to run once:
  
}

USE_EVENTUALLY_LOOP(app.mgr)