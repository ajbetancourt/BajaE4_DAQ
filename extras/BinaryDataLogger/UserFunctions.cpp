#include "UserTypes.h"
//#include <Adafruit_LIS3DH_TRIM.h>
#include <Wire.h>
// User data functions.  Modify these functions for your data items.

// Start time for data
static uint32_t startMicros;

//Sensor objects
//Adafruit_LIS3DH accelerometer = Adafruit_LIS3DH();

// Acquire a data record.
void acquireData(data_t* data) {
  data->time = micros();

  //RPM data
  Wire.requestFrom(8,2);
  delayMicroseconds(50);
  uint32_t revicedValue = Wire.read() << 8;
    revicedValue +=Wire.read();
  data->sensor_data[0] = revicedValue;
  // for (int i = 0; i < 2; i++) {
  //   data->sensor_data[i] = analogRead(i);
  // }

  // //accelerometer data
  // accelerometer.read();
  // data->sensor_data[2] = accelerometer.x;
  // data->sensor_data[3] = accelerometer.y;
  // data->sensor_data[4] = accelerometer.z;

  // for (int i = 5; i < NUM_SENSORS; i++) {
  //   data->sensor_data[i] = analogRead(3);
  // }
}

// Print a data record.
void printData(Print* pr, data_t* data) {
  if (startMicros == 0) {
    startMicros = data->time;
  }
  pr->print(data->time - startMicros);
  for (int i = 0; i < NUM_SENSORS; i++) {
    pr->write(',');
    pr->print(data->sensor_data[i]);
  }
  pr->println();
}

// Print data header.
void printHeader(Print* pr) {
  startMicros = 0;
  pr->print(F("Micros"));
  pr->print(F(",Engine RPM"));
  //pr->print(F(",Secondary RPM"));
  //pr->print(F(",X,Y,Z"));
  //pr->print(F(",FL Strain,FR Strain,RL Strain,RR Strain"));
  //pr->print(F(",FL Shock Pos,FR Shock Pos,RL Shock Pos,RR Shock Pos"));
  //Move to next line to for data
  pr->println();
}

// Sensor setup
void userSetup() {
  //Analog values from 0 to 4096
  //analogReadResolution(12);

  //Set up accelerometer
  //Using i2c
  // if(!accelerometer.begin(0x18))
  // {
  //   //Throw error
  // }
  // accelerometer.setRange(LIS3DH_RANGE_4_G);
  Wire.begin();
}
