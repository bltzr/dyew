#include <Arduino.h>
#include <CapacitiveSensor.h>
#include <OSCBundle.h>
#include <EEPROM.h>

//Teensy and Leonardo variants have special USB serial
#if defined(CORE_TEENSY)|| defined(__AVR_ATmega32U4__)
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(Serial);
#else
// any hardware serial port
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif

/*
 * Antoine Villeret - 2016
 * project : Dyew by Djeff Regottaz
 * based on capsense+fsr.cpp from _Das Körperrauschen_ project
 *
 * Scan capacitive and FSR sensors and send values
 * through USB in OSC SLIP-encoded messages.
 * Also drive an LED through output D6
 * Uses a high value resistor e.g. 40M between send pin and receive pin.
 * Resistor affects sensitivity, experiment with values, 50K - 50M.
 * Larger resistor values yield larger sensor values.
 * Receive pin is the sensor pin - try different amounts of
 * foil/metal on this pin.
 *
 */

#define COMMON_PIN 13
#define MAX_STRING_LENGTH 32

// prototypes
void LEDcontrol(OSCMessage &msg, int addrOffset);
void CScontrol(OSCMessage &msg, int addrOffset);
void SystemControl(OSCMessage &msg, int addrOffset);
void setup();
void receiveOSC();
void loop();
void getAllParams();
void saveParam();
void loadParam();


// global variables

char path[MAX_STRING_LENGTH];
unsigned char offset=0;

long start_scan=0;

// 40M resistor between COMMON_PIN & sensor pin, for each sensor
CapacitiveSensor   cs[] = { CapacitiveSensor(COMMON_PIN,0), /* CS0 */ \
                            CapacitiveSensor(COMMON_PIN,1), /* CS1 */ \
                            CapacitiveSensor(COMMON_PIN,2), /* CS2 */ \
                            CapacitiveSensor(COMMON_PIN,3), /* CS3 */ \
                            CapacitiveSensor(COMMON_PIN,4), /* CS4 */ \
                            CapacitiveSensor(COMMON_PIN,5), /* CS5 */ \
                            CapacitiveSensor(COMMON_PIN,7), /* CS6 */ \
                            CapacitiveSensor(COMMON_PIN,8), /* CS7 */ \
                            CapacitiveSensor(COMMON_PIN,11)}; /* CS8 */
#define CS_COUNT 9 // number of CapacitiveSensor occurence above...
static const int fsr_pin[] = {A0,A1,A2,A3,A4,A5,A11,A10,A9}; // FSR pin
#define FSR_COUNT 9
#define LED_PIN 6

/* global parameters */
// since this struct is saved 'as is' in EEPROM, please add new element at the end
// otherwise you'll have to rewrite them all..
typedef struct {
  uint8_t address;
  bool fsr_enable[15]; // 15 > 9 : take a little overhead in case we want to upgrade board...
  bool fsr_on;
  bool cs_enable[15];
  bool cs_on;

  uint16_t cs_sensibility;
  uint16_t cs_timeout;
  unsigned long cs_autocal;
  uint8_t led_brightess;
  uint8_t speedlimit;
} BoardParam;
/* end of global parameters */

BoardParam boardParam;

int i=0, cs_id;
long rawcount=0;
float cs_norm[9] ={0.,0.,0.,0.,0.,0.,0.,0.,0.};
int fsr[9]={0,0,0,0,0,0,0,0,0};
OSCBundle bundleOUT;

/* parse message then with /led prefix */
void LEDcontrol(OSCMessage &msg, int addrOffset)
{
  if (msg.size() == 0){
    snprintf(path+offset,MAX_STRING_LENGTH-offset,"/led");
    bundleOUT.add(path).add(boardParam.led_brightess);
  } else if (msg.isInt(0)) {
    boardParam.led_brightess = msg.getInt(0);
    analogWrite(LED_PIN, boardParam.led_brightess);
  }
}

/* parse message then with /cs prefix */
void CScontrol(OSCMessage &msg, int addrOffset)
{
  if (msg.fullMatch("/on",addrOffset)) {
    if (msg.size() == 0){
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs/on");
      bundleOUT.add(path).add(boardParam.cs_on);
    } else if (msg.isInt(0)){
      boardParam.cs_on = msg.getInt(0) > 0;
    }
  } else if ( msg.fullMatch("/enable", addrOffset) ) {
    if ( msg.size() == 0 ){
      // if /enable is sent without any arguments, then send back active sensors list
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs/enable");
      OSCMessage msgOUT(path);
      for (i=0;i<CS_COUNT;i++){
        msgOUT.add(boardParam.cs_enable[i]);
      }
      msgOUT.send(SLIPSerial);
      SLIPSerial.endPacket();
      msgOUT.empty();
    } else {
      // else treat values list to enable/disable sensors
      for (i=0;i<msg.size() || i<CS_COUNT;i++){
        boardParam.cs_enable[i] = msg.getInt(i) > 0;
      }
    }
  } else if ( msg.fullMatch("/sensibility", addrOffset) ) {
    if ( msg.size() == 0 ){
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs/sensibility");
      bundleOUT.add(path).add( (int) boardParam.cs_sensibility);
    } else if (msg.isInt(0)) {
      boardParam.cs_sensibility = msg.getInt(0) > 0 ? msg.getInt(0) : 1;
    }
  } else if ( msg.fullMatch("/timeout", addrOffset) ) {
    if (msg.size() == 0){
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs/timeout");
      bundleOUT.add(path).add((int)boardParam.cs_timeout);
    } else if (msg.isInt(0)) {
      boardParam.cs_timeout=msg.getInt(0) > 0 ? msg.getInt(0) : 1;
      for ( i=0; i<CS_COUNT ; i++)
      {
        cs[i].set_CS_Timeout_Millis(boardParam.cs_timeout);
      }
    }
  }
}

/* parse message then with /s (aka system) prefix */
void SystemControl(OSCMessage &msg, int addrOffset){
  if (msg.fullMatch("/save",addrOffset)){
    saveParam();
  } else if (msg.fullMatch("/load",addrOffset)){
    loadParam();
  } else if ( msg.fullMatch("/getParams",addrOffset) ){
    getAllParams();
  } else if ( msg.fullMatch("/speedlimit",addrOffset) ){
    if (msg.size() == 0){
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/s/speedlimit");
      bundleOUT.add(path).add((int)boardParam.speedlimit);
    } else if (msg.isInt(0)) {
      boardParam.speedlimit=msg.getInt(0) > 0 ? msg.getInt(0) : 0;
    }
  } else {
    // send back build data and time
    snprintf(path+offset,MAX_STRING_LENGTH-offset,"/s/date");
    bundleOUT.add(path).add(__DATE__);
    snprintf(path+offset,MAX_STRING_LENGTH-offset,"/s/time");
    bundleOUT.add(path).add(__TIME__);
  }
}

/* send back all parameters throught OSC */
void getAllParams(){
  OSCMessage get_on("/on");
  CScontrol(get_on,0);

  OSCMessage get_enable("/enable");
  CScontrol(get_enable,0);

  OSCMessage get_sensibility("/sensibility");
  CScontrol(get_sensibility,0);

  OSCMessage get_timeout("/timeout");
  CScontrol(get_timeout,0);

  OSCMessage get_speedlimit("/speedlimit");
  SystemControl(get_speedlimit,0);

  OSCMessage get_led("/led");
  LEDcontrol(get_led,0);
}

/* save all parameters to EEPROM */
void saveParam(){
  eeprom_write_block(&boardParam, 0, sizeof(BoardParam));
}

/* load parameters form EEPROM and send them through OSC */
void loadParam(){
  eeprom_read_block(&boardParam, 0, sizeof(BoardParam));

  // some garde fou
  if ( boardParam.cs_timeout > 2000 ) boardParam.cs_timeout=100;
  if ( boardParam.cs_sensibility > 2000 ) boardParam.cs_sensibility=30;

  getAllParams();
}

/* needed Setup() function */
void setup()
{
  Serial.begin(115200);
  while(!Serial); // wait until a serial connection is opened

  loadParam();
  offset = sprintf(path,"/b%02d",boardParam.address);

  for ( i=0; i<FSR_COUNT; i++){
    boardParam.fsr_enable[i]=true;
  }
  for ( i=0; i<CS_COUNT ; i++)
  {
    boardParam.cs_enable[i]=true;
    boardParam.cs_autocal=0xFFFFFFFF; // switch off autocalibration : use Pd's filter instead
    cs[i].set_CS_AutocaL_Millis(boardParam.cs_autocal);
    cs[i].set_CS_Timeout_Millis(boardParam.cs_timeout);
  }
}

/* read serial, decode SLIP and dispatch message */
void receiveOSC(){
  OSCBundle bundleIN;
  if ( SLIPSerial.available() > 0 ){
    while(!SLIPSerial.endofPacket()){
      int size=0;
      if( (size = SLIPSerial.available()) > 0)
      {
         while(size--)
            bundleIN.fill(SLIPSerial.read());
      }
    }

    if(!bundleIN.hasError()) {
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs");
      bundleIN.route(path, CScontrol);
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/s");
      bundleIN.route(path, SystemControl);
      snprintf(path+offset,MAX_STRING_LENGTH-offset,"/led");
      bundleIN.route(path, LEDcontrol);
    }
  }

}

/* scan capacitive sensors */
void scanCS()
{
  if ( boardParam.cs_on ){
    for ( cs_id=0 ; cs_id < CS_COUNT; cs_id++ ){
      int j = CS_COUNT;
      if ( cs_id == 8 ){
          rawcount = cs[cs_id].capacitiveSensorRaw(boardParam.cs_sensibility);
          if (rawcount != -2){ // if sensor is timeout, resend last value
            cs_norm[cs_id] = (float) rawcount / (float) boardParam.cs_sensibility;
          } else {
            cs_norm[cs_id] = -2;
          }
      } else { // sensor is disabled
        cs_norm[cs_id] = -1;
      }
    }
    snprintf(path+offset,MAX_STRING_LENGTH-offset,"/cs");
    bundleOUT.add(path).add(cs_norm[0]).add(cs_norm[1]).add(cs_norm[2]).add(cs_norm[3]).add(cs_norm[4]).add(cs_norm[5]).add(cs_norm[6]).add(cs_norm[7]).add(cs_norm[8]);
  }
}


/* classic arduino loop */
void loop()
{
  receiveOSC();

  scanCS();

  long end_scan = millis();
  // compute and send scan loop duration over OSC
  snprintf(path+offset,MAX_STRING_LENGTH-offset,"/s/t");
  bundleOUT.add(path).add(end_scan - start_scan);
  while ( (millis() - start_scan) < boardParam.speedlimit ){
	;
  }
  start_scan = millis();

  bundleOUT.send(SLIPSerial);
  SLIPSerial.endPacket();
  bundleOUT.empty();
}

