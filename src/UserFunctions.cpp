#include "UserTypes.h"
#include <Wire.h>
#include "Adafruit_LIS3DH.h"
#include "Adafruit_Sensor.h"
// User data functions.  Modify these functions for your data items.

// Start time for data
static uint32_t startMicros;

//RPM Sensor addresses
uint8_t rpmSensorsAdd[RPM_DIM] = {0,1,2};

Adafruit_LIS3DH accel = Adafruit_LIS3DH();

bool record = false;

// Acquire a data record.
void acquireData(data_t* data) 
{
  data->time = micros();

  accel.read();
  for(int i = 0; i < RPM_DIM; i++)
  {
    data->rpm[i] = getRPMSensorData(rpmSensorsAdd[i]);
    delayMicroseconds(75);
  }
  data->accel[0] = accel.x;
  data->accel[1] = accel.y;
  data->accel[2] = accel.z;
}

// Print a data record.
void printData(Print* pr, data_t* data) {
  if (startMicros == 0) {
    startMicros = data->time;
  }
  pr->print(data->time - startMicros);
  for (int i = 0; i < RPM_DIM; i++) {
    pr->write(',');
    pr->print(data->rpm[i]);
  }
  for (int i = 0; i < 3; i++) {
    pr->write(',');
    pr->print(data->accel[i]);
  }
  pr->println();
}

// Print data header.
void printHeader(Print* pr) {
  startMicros = 0;
  pr->print(F("micros"));
  for (int i = 0; i < RPM_DIM; i++) {
    pr->print(F(",adc"));
    pr->print(i);
  }
  pr->print(F(",x accel"));
  pr->print(F(",y accel"));
  pr->print(F(",z accel"));
  pr->println();
}

// Sensor setup
boolean userSetup()
{
  boolean accelConnected = accel.begin(0x18);
  delay(10);
  accel.setRange(LIS3DH_RANGE_4_G);

  Wire.begin();
  Wire.setClock(400000);
  delay(10);

  //Check if all sensors are responding
  boolean allSensorsConn = true;
  if(!accelConnected)
  {
    Serial.println("Accelerameter not connected");
    allSensorsConn = false;
  }

  for(int i = 0; i < RPM_DIM; i++)
  {
    if(getRPMSensorData(rpmSensorsAdd[i]) == (2^16)-1)
    {
      Serial.print("Sensor ");
      Serial.print(i);
      Serial.println(" isn't responding");
      allSensorsConn = false;
    }
  }
  return allSensorsConn;
}

uint16_t getRPMSensorData(uint8_t address)
{
    Wire.requestFrom(address,RPM_SENSOR_BYTES);
    
    //Give time for sensor to respond
    uint32_t startTime = micros();
    boolean timedOut = false;

    while(!timedOut && !Wire.available())
    {
      if(micros() - startTime >= 500)
        timedOut = true;
    }

    if(timedOut)
    {
      //If timed out then return a large number that the device will never see in real life
      //A frequency of 65535 is out of reach for the baja car
      return (2^16)-1;
    }

    //Reaad in data if there wasn't a timeout
    uint8_t msb = Wire.read();
    uint8_t lsb = Wire.read();

    return (msb << 8) + lsb;
}

