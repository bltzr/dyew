#include "EEPROM.h"
#define address 0
void setup()
{
  pinMode(13,OUTPUT);
  digitalWrite(13,LOW);
  EEPROM.write(0,address);
  digitalWrite(13,HIGH);
}

void loop()
{
}
