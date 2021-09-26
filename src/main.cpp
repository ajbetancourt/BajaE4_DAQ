#include <Arduino.h>

#include <SPI.h>
#include "SdFat.h"
#include "FreeStack.h"
#include "UserTypes.h"
#include "main.h"
#include "SerialCmds.h"

//this is a git test

//==============================================================================
// Start of configuration constants.
//==============================================================================
// Abort run on an overrun.  Data before the overrun will be saved.
#define ABORT_ON_OVERRUN 1
//------------------------------------------------------------------------------
//Interval between data records in microseconds.
const uint32_t LOG_INTERVAL_USEC = 10000; //Number is in milliseconds
//------------------------------------------------------------------------------
// Set USE_SHARED_SPI non-zero for use of an SPI sensor.
// May not work for some cards.
#ifndef USE_SHARED_SPI
#define USE_SHARED_SPI 0
#endif  // USE_SHARED_SPI
//------------------------------------------------------------------------------
// Pin definitions.
//
// SD chip select pin.
const uint8_t SD_CS_PIN = 2;
//
// Digital pin to indicate an error, set to -1 if not used.
// The led blinks for fatal errors. The led goes on solid for
// overrun errors and logging continues unless ABORT_ON_OVERRUN
// is non-zero.
#ifdef ERROR_LED_PIN
#undef ERROR_LED_PIN
#endif  // ERROR_LED_PIN
const int8_t ERROR_LED_PIN = LED_BUILTIN;
//------------------------------------------------------------------------------
// File definitions.
//
// Maximum file size in blocks.
// The program creates a contiguous file with FILE_BLOCK_COUNT 512 byte blocks.
// This file is flash erased using special SD commands.  The file will be
// truncated if logging is stopped early.
const uint32_t FILE_BLOCK_COUNT = 256000;
//
// log file base name if not defined in UserTypes.h
#ifndef FILE_BASE_NAME
#define FILE_BASE_NAME "data"
#endif  // FILE_BASE_NAME
//------------------------------------------------------------------------------
// Buffer definitions.
//
// The logger will use SdFat's buffer plus BUFFER_BLOCK_COUNT-1 additional
// buffers.
//
const uint8_t BUFFER_BLOCK_COUNT = 12;
//==============================================================================
// End of configuration constants.
//==============================================================================
// Temporary log file.  Will be deleted if a reset or power failure occurs.
#define TMP_FILE_NAME FILE_BASE_NAME "##.bin"

// Size of file base name.
const uint8_t BASE_NAME_SIZE = sizeof(FILE_BASE_NAME) - 1;
const uint8_t FILE_NAME_DIM  = BASE_NAME_SIZE + 8;
char binName[FILE_NAME_DIM] = FILE_BASE_NAME "00.bin";
const char FILE_EXT[5] = ".bin";

SdFat sd;
SdBaseFile binFile;

// Number of data records in a block.
const uint16_t DATA_DIM = (512 - 4)/sizeof(data_t);

//Compute fill so block size is 512 bytes.  FILL_DIM may be zero.
const uint16_t FILL_DIM = 512 - 4 - DATA_DIM*sizeof(data_t);

struct block_t {
  uint16_t count;
  uint16_t overrun;
  data_t data[DATA_DIM];
  uint8_t fill[FILL_DIM];
};
//==============================================================================
// Error messages stored in flash.
#define error(msg) {sd.errorPrint(&Serial, F(msg));fatalBlink();}

//Anthony's variables
const uint8_t recordSwitch = 7;
boolean recording = false;
boolean wifiTransfer = false;
boolean connected = false;
bool newFiles = false;
#define USE_WIFI true
#define MAX_FILES 100
#define SERIAL1_SPD 250000
bool buttonActive = false;
bool butLeftPressed = false;
bool butRightPressed = false;
bool longPressActive = false;
uint32_t buttonHeldTimer = 0;
uint32_t buttonTriggerLen = 500;

#define buttonLeft 12
#define buttonRight 11

//Sets how long the recording will be from pressing the steering wheel buttons
#define recordingLenSec 15
const unsigned long recordTimeLen = recordingLenSec*1000000;
long recordMenuRecAmt = 0; //How long it has been recording


void startRecording() {
  recording = true;
}

void allToCSV() {
  boolean moreFiles = true;
  uint8_t fileCounter = 0;
  char name[FILE_NAME_DIM];
  binFile.close(); //close any open file
  while(moreFiles) {
    sprintf(name,FILE_BASE_NAME "%02d.bin",fileCounter);
    Serial.println(name);
    if(sd.exists(name)) {
      binaryToCsv(name);
      fileCounter++;
    }
    else {
      Serial.println("All files converted");
      moreFiles = false;
    }
  }
}

void fatalBlink() {
  while (true) {
    SysCall::yield();
    if (ERROR_LED_PIN >= 0) {
      digitalWrite(ERROR_LED_PIN, HIGH);
      delay(200);
      digitalWrite(ERROR_LED_PIN, LOW);
      delay(200);
    }
  }
}
//------------------------------------------------------------------------------
void binaryToCsv(char* fileName) {
    uint8_t lastPct = 0;
    block_t block;
    uint32_t t0 = millis();
    uint32_t syncCluster = 0;
    SdFile csvFile;
    char csvName[FILE_NAME_DIM];

    // Create a new csvFile.
    strcpy(csvName, fileName);
    strcpy(&csvName[BASE_NAME_SIZE + 3], "csv");

    if (!csvFile.open(csvName, O_WRONLY | O_CREAT | O_TRUNC)) {
      error("open csvFile failed");
    }
    binFile.rewind();
    Serial.print(F("Writing: "));
    Serial.print(csvName);
    Serial.println(F(" - type any character to stop"));
    printHeader(&csvFile);
    uint32_t tPct = millis();
    while (!Serial.available() && binFile.read(&block, 512) == 512) {
      uint16_t i;
      if (block.count == 0 || block.count > DATA_DIM) {
        break;
      }
      if (block.overrun) {
        csvFile.print(F("OVERRUN,"));
        csvFile.println(block.overrun);
      }
      for (i = 0; i < block.count; i++) {
        printData(&csvFile, &block.data[i]);
      }
      if (csvFile.curCluster() != syncCluster) {
        csvFile.sync();
        syncCluster = csvFile.curCluster();
      }
      if ((millis() - tPct) > 1000) {
        uint8_t pct = binFile.curPosition()/(binFile.fileSize()/100);
        if (pct != lastPct) {
          tPct = millis();
          lastPct = pct;
          Serial.print(pct, DEC);
          Serial.println('%');
        }
      }
      if (Serial.available()) {
        break;
      }
    }
    csvFile.close();
    binFile.close();
    Serial.print(F("Done: "));
    Serial.print(0.001*(millis() - t0));
    Serial.println(F(" Seconds"));
}
//-----------------------------------------------------------------------------
void createBinFile() {
  // max number of blocks to erase per erase call
  const uint32_t ERASE_SIZE = 262144L;
  uint32_t bgnBlock, endBlock;

  // Delete old tmp file.
  if (sd.exists(TMP_FILE_NAME)) {
    Serial.println(F("Deleting tmp file " TMP_FILE_NAME));
    if (!sd.remove(TMP_FILE_NAME)) {
      error("Can't remove tmp file");
    }
  }
  // Create new file.
  Serial.println(F("\nCreating new file"));
  binFile.close();
  if (!binFile.createContiguous(TMP_FILE_NAME, 512 * FILE_BLOCK_COUNT)) {
    error("createContiguous failed");
  }
  // Get the address of the file on the SD.
  if (!binFile.contiguousRange(&bgnBlock, &endBlock)) {
    error("contiguousRange failed");
  }
  // Flash erase all data in the file.
  Serial.println(F("Erasing all data"));
  uint32_t bgnErase = bgnBlock;
  uint32_t endErase;
  while (bgnErase < endBlock) {
    endErase = bgnErase + ERASE_SIZE;
    if (endErase > endBlock) {
      endErase = endBlock;
    }
    if (!sd.card()->erase(bgnErase, endErase)) {
      error("erase failed");
    }
    bgnErase = endErase + 1;
  }
}
//------------------------------------------------------------------------------
void logData() {
  createBinFile();
  recordBinFile();
  renameBinFile();
}
//------------------------------------------------------------------------------
void recordBinFile() {
  const uint8_t QUEUE_DIM = BUFFER_BLOCK_COUNT + 1;
  // Index of last queue location.
  const uint8_t QUEUE_LAST = QUEUE_DIM - 1;

  // Allocate extra buffer space.
  block_t block[BUFFER_BLOCK_COUNT - 1];

  block_t* curBlock = 0;

  block_t* emptyStack[BUFFER_BLOCK_COUNT];
  uint8_t emptyTop;
  uint8_t minTop;

  block_t* fullQueue[QUEUE_DIM];
  uint8_t fullHead = 0;
  uint8_t fullTail = 0;

  // Use SdFat's internal buffer.
  emptyStack[0] = (block_t*)sd.vol()->cacheClear();
  if (emptyStack[0] == 0) {
    error("cacheClear failed");
  }
  // Put rest of buffers on the empty stack.
  for (int i = 1; i < BUFFER_BLOCK_COUNT; i++) {
    emptyStack[i] = &block[i - 1];
  }
  emptyTop = BUFFER_BLOCK_COUNT;
  minTop = BUFFER_BLOCK_COUNT;

  // Start a multiple block write.
  if (!sd.card()->writeStart(binFile.firstBlock())) {
    error("writeStart failed");
  }
  Serial.print(F("FreeStack: "));
  Serial.println(FreeStack());
  Serial.println(F("Logging - type any character to stop"));
  bool closeFile = false;
  uint32_t bn = 0;
  uint32_t maxLatency = 0;
  uint32_t overrun = 0;
  uint32_t overrunTotal = 0;
  uint32_t logTime = micros();
  uint32_t startTime = logTime;
  while(1) {
     // Time for next data record.
    logTime += LOG_INTERVAL_USEC;

    //Check if the buttons were pressed to stop recording
    //checkButtons(buttonLeft,buttonRight);

    //Check if recording time has been longer than recordTimeLen
    if(logTime > startTime + recordTimeLen) {
      recording = false;
    }

    if (recording == false) {
      closeFile = true;
    }
    if (closeFile) {
      if (curBlock != 0) {
        // Put buffer in full queue.
        fullQueue[fullHead] = curBlock;
        fullHead = fullHead < QUEUE_LAST ? fullHead + 1 : 0;
        curBlock = 0;
      }
    } else {
      if (curBlock == 0 && emptyTop != 0) {
        curBlock = emptyStack[--emptyTop];
        if (emptyTop < minTop) {
          minTop = emptyTop;
        }
        curBlock->count = 0;
        curBlock->overrun = overrun;
        overrun = 0;
      }
      if ((int32_t)(logTime - micros()) < 0) {
        error("Rate too fast");
      }
      int32_t delta;
      do {
        delta = micros() - logTime;
      } while (delta < 0);
      if (curBlock == 0) {
        overrun++;
        overrunTotal++;
        if (ERROR_LED_PIN >= 0) {
          digitalWrite(ERROR_LED_PIN, HIGH);
        }
        #if ABORT_ON_OVERRUN
                Serial.println(F("Overrun abort"));
                break;
        #endif  // ABORT_ON_OVERRUN
            } else {
        #if USE_SHARED_SPI
                sd.card()->spiStop();
        #endif  // USE_SHARED_SPI
                acquireData(&curBlock->data[curBlock->count++]);
        #if USE_SHARED_SPI
                sd.card()->spiStart();
        #endif  // USE_SHARED_SPI
        if (curBlock->count == DATA_DIM) {
          fullQueue[fullHead] = curBlock;
          fullHead = fullHead < QUEUE_LAST ? fullHead + 1 : 0;
          curBlock = 0;
        }
      }
    }
      if (fullHead == fullTail) {
        // Exit loop if done.
        if (closeFile) {
          break;
        }
      } else if (!sd.card()->isBusy()) {
        // Get address of block to write.
        block_t* pBlock = fullQueue[fullTail];
        fullTail = fullTail < QUEUE_LAST ? fullTail + 1 : 0;
        // Write block to SD.
        uint32_t usec = micros();
        if (!sd.card()->writeData((uint8_t*)pBlock)) {
          error("write data failed");
        }
        usec = micros() - usec;
        if (usec > maxLatency) {
          maxLatency = usec;
        }
        // Move block to empty queue.
        emptyStack[emptyTop++] = pBlock;
        bn++;
        if (bn == FILE_BLOCK_COUNT) {
          // File full so stop
          break;
        }
      }
    }
    if (!sd.card()->writeStop()) {
      error("writeStop failed");
    }
    Serial.print(F("Min Free buffers: "));
    Serial.println(minTop);
    Serial.print(F("Max block write usec: "));
    Serial.println(maxLatency);
    Serial.print(F("Overruns: "));
    Serial.println(overrunTotal);
    // Truncate file if recording stopped early.
    if (bn != FILE_BLOCK_COUNT) {
      Serial.println(F("Truncating file"));
      if (!binFile.truncate(512L * bn)) {
        error("Can't truncate file");
      }
    }
}
//------------------------------------------------------------------------------
void recoverTmpFile() {
  uint16_t count;
  if (!binFile.open(TMP_FILE_NAME, O_RDWR)) {
    return;
  }
  if (binFile.read(&count, 2) != 2 || count != DATA_DIM) {
    error("Please delete existing " TMP_FILE_NAME);
  }
  Serial.println(F("\nRecovering data in tmp file " TMP_FILE_NAME));
  uint32_t bgnBlock = 0;
  uint32_t endBlock = binFile.fileSize()/512 - 1;
  // find last used block.
  while (bgnBlock < endBlock) {
    uint32_t midBlock = (bgnBlock + endBlock + 1)/2;
    binFile.seekSet(512*midBlock);
    if (binFile.read(&count, 2) != 2) error("read");
    if (count == 0 || count > DATA_DIM) {
      endBlock = midBlock - 1;
    } else {
      bgnBlock = midBlock;
    }
  }
  // truncate after last used block.
  if (!binFile.truncate(512*(bgnBlock + 1))) {
    error("Truncate " TMP_FILE_NAME " failed");
  }
  renameBinFile();
}
//-----------------------------------------------------------------------------
void renameBinFile() {
  while (sd.exists(binName)) {
    if (binName[BASE_NAME_SIZE + 1] != '9') {
      binName[BASE_NAME_SIZE + 1]++;
    } else {
      binName[BASE_NAME_SIZE + 1] = '0';
      if (binName[BASE_NAME_SIZE] == '9') {
        error("Can't create file name");
      }
      binName[BASE_NAME_SIZE]++;
    }
  }
  if (!binFile.rename(binName)) {
    error("Can't rename file");
    }
  Serial.print(F("File renamed: "));
  Serial.println(binName);
  Serial.print(F("File size: "));
  Serial.print(binFile.fileSize()/512);
  Serial.println(F(" blocks"));
}
//------------------------------------------------------------------------------
void testSensor() {
  const uint32_t interval = 200000;
  int32_t diff;
  data_t data;
  Serial.println(F("\nTesting - type any character to stop\n"));
  // Wait for Serial Idle.
  delay(1000);
  printHeader(&Serial);
  uint32_t m = micros();
  while (!Serial.available()) {
    m += interval;
    do {
      diff = m - micros();
    } while (diff > 0);
    acquireData(&data);
    printData(&Serial, &data);
  }
}
//------------------------------------------------------------------------------
bool connectWifi() {
  uint32_t startTime = millis();
  Serial.println("Connecting to Wifi module");
  while((millis() - startTime < 5000) && !connected)
  {
    if(Serial1.available()>0 && Serial1.readStringUntil(EOL).compareTo(RDY) == 0)
    {
      //WIFI is connected
      sendCmd(ACK,true,false);
      if(waitForACK(1000))
      {
        sendCmd(ACK,true,false);
        connected = true;
        break;
      }
    }
  }
  if(connected)
    Serial.println("Wifi module connected");
  else
    Serial.println("Wifi NOT module connected");
  Serial1.flush(); //Clear all commands incase extra were sent
  return connected;
}

bool waitForACK(uint32_t timeout) {
  uint32_t startTime = millis();
  while((millis() - startTime) < timeout)
  {
    if(Serial1.available()>0)
    {
      if(Serial1.readStringUntil(EOL).compareTo(ACK) == 0)
      {
        //Serial.println("ACK recieved");
        return true;
      }
    }
  }
  Serial.println("NAK");
  return false;
}

bool getCMD(String& incomingCmd,uint32_t timeout) {
  uint32_t startTime = millis();
  while(millis() - startTime < timeout)
  {
    if(Serial1.available() > 0)
    {
      incomingCmd = Serial1.readStringUntil(EOL);
      return true;
    }
  }
  return false;
}

void sendCmd(String cmd,boolean addEOL=true,bool printCMD = false) {
  Serial1.print(cmd);
  if(printCMD)
    Serial.println(cmd);
  if(addEOL) {
    Serial1.print(EOL);
  }
}

void transferFileNames() {
  SdFile root;
    SdFile file;
    root.open("/");
  char fileName[13];

  //Send all csv file over
  Serial.println("Transfering file names");
  sendCmd(RDY);
  if(waitForACK(1000)) {
    Serial.println("Transfer started");
    while (file.openNext(&root, O_READ)) {
      if (file.isFile()) {
        file.getName(fileName,13);
        if (strcmp(FILE_EXT, &fileName[strlen(fileName)-strlen(FILE_EXT)]) == 0) {
          //File is a csv file
          sendCmd(fileName,true,true); //Send over files name
          if(!waitForACK(1000)) {
            Serial.println("Wifi didn't respond during transfer");
            break;
          }
        }
      }
      file.close();
    }
    sendCmd(END,true,false); //End tranfer
  }
}

void transferFile() {
  //Get file name from wifi module
  String fileName;
  if(!getCMD(fileName,1000))
    Serial.println("No response");

  char fileNameChar[FILE_NAME_DIM];
  fileName.toCharArray(fileNameChar,FILE_NAME_DIM);

  //Convert file to csv
  if(sd.exists(fileNameChar))
  {
    binFile.close(); //Close any open files just in case
    
    if(!binFile.open(fileNameChar)) { //Open the file requested
      Serial.println("Couldn't open bin file");
      return;
    }
    Serial.println("File opened");

    sendCmd(RDY,true);
    if(!waitForACK(5000)) {
      Serial.println("Not ready");
      return;
    }

    Serial.println("Sending file size");
    //Send size of file
    uint32_t fileSize = binFile.fileSize(); //In bytes
    sendCmd(String(fileSize));
    if(!waitForACK(5000)) {
      Serial.println("Not ready after file size");
      return;
    }

    block_t block;
    binFile.rewind(); //Go to the begining of the file
    printHeader(&Serial1);
    Serial1.print(EOL);

    while (binFile.read(&block , 512) == 512) {
      if (block.count == 0) {
        break;
      }
      for (uint16_t i = 0; i < block.count; i++) {
        printData(&Serial1, &block.data[i]);
        Serial1.print(EOL);
        if(!waitForACK(5000))
        {
          Serial.println("No response");
          return;
        }
      }
    }
  }
  else{
    Serial.println("File doesn't exist");
  }

  sendCmd(END,true);
}

void deleteFile() {
  //Get file name from wifi module
  Serial.println("Deleting file");
  String fileName;
  if(!getCMD(fileName,1000))
    Serial.println("No response");

  char fileNameChar[FILE_NAME_DIM];
  fileName.toCharArray(fileNameChar,FILE_NAME_DIM);

  //Convert file to csv
  if(sd.exists(fileNameChar))
  {
    if(sd.remove(fileNameChar)) {
      sendCmd(ACK); //Deletion was completed
    }
    else {
      //Deletion error
      sendCmd(ERR);
    }
  }
}

void setup() {
    if (ERROR_LED_PIN >= 0) {
        pinMode(ERROR_LED_PIN, OUTPUT);
    }
    Serial.begin(115200);
    while(!Serial)
    {
      SysCall::yield();
    }

    // Allow userSetup access to SPI bus.
    pinMode(SD_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);

    pinMode(buttonLeft,INPUT);
    pinMode(buttonRight,INPUT);

    // Setup sensors.
    delay(1000); //Give the sensors a chance to start up
    if (!userSetup())
    {
        fatalBlink();
    }

    // Initialize at the highest speed supported by the board that is
    // not over 50 MHz. Try a lower speed if SPI errors occur.
    if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(50))) {
        sd.initErrorPrint(&Serial);
        fatalBlink();
    }

    if(sd.exists(TMP_FILE_NAME))
    {
        //Always recover a file. It might be useful
        recoverTmpFile();
        delay(200);
    }
    //Define recording switch as a interrupt 
    pinMode(recordSwitch,INPUT);

    //Transfer file names to esp32
    #if USE_WIFI
    Serial1.begin(SERIAL1_SPD);
    if(connectWifi())
    {
      delay(1000);
      //transferFileNames();
      //Serial.println("Done");
    }    
    #else 
    Serial.println("Wifi disabled");
    #endif
    
}

void checkButtons(int butLeft, int butRight) {
  bool butLeftState = !digitalRead(butLeft);
  bool butRightState = !digitalRead(butRight);

  if(butLeftState == true)
  {
    //If left button is pressed change its state
    if(buttonActive == false)
    {
      buttonActive = true;
      buttonHeldTimer = millis();
      Serial.println("Left Pressed");
    }
    butLeftPressed = true;
    //Serial.println("Left Pressed");
  }

  if(butRightState == true)
  {
    //If right button is pressed change its state
    if(buttonActive == false)
    {
      buttonActive = true;
      buttonHeldTimer = millis();
      Serial.println("Right button pressd");
    }
    butRightPressed=true;
  }

  if((buttonActive == true && (millis() - buttonHeldTimer > buttonTriggerLen))
      && longPressActive == false)
  {
    //If any button is pressed and the button held timer is greater than the
    //held button length. Enable the long press
    if(buttonLeft && buttonRight) {
      Serial.println("Double button long press active");
    
      longPressActive = true;
      //If on the record menu and holding both buttons down, start recording
      recording = !recording;
    }
    
  }

  if(buttonActive == true && (butLeftState == false && butRightState == false)) {
    //If a button was pressed in the previous loop but now none are pressed
    //Then disable the long press and change the button state vars
    if(longPressActive == true) {
      Serial.println("Long press stopped");
      longPressActive = false;
    }

    buttonActive = false;
    butLeftPressed = false;
    butRightPressed = false;
  }
}

void loop() {
    //Do nothing so the esp can do wifi related stuff

    //recording = !digitalRead(recordSwitch);
    checkButtons(buttonLeft,buttonRight);
    if(recording) {
      #if USE_WIFI
      Serial1.flush();
      Serial1.end();
      #endif
      delay(2000);
      
      digitalWrite(LED_BUILTIN,HIGH);
      logData();
      recording=false;
      digitalWrite(LED_BUILTIN,LOW);
      
      delay(5);
      #if USE_WIFI
      Serial1.begin(SERIAL1_SPD);
      Serial1.flush();
      #endif
    }
    else if(Serial1.available() > 0)
    {
      //Wifi module sent something
      String cmd = Serial1.readStringUntil(EOL);
      if(cmd.compareTo(FNAME) == 0)
      {
        //Transfer file names
        sendCmd(ACK,true);
        transferFileNames();
      }
      else if(cmd.compareTo(FDATA) == 0)
      {
        //Transfer file data
        sendCmd(ACK,true);
        transferFile();
      }
      else if(cmd.compareTo(DEL) == 0)
      {
        sendCmd(ACK);
        deleteFile();
      }
      Serial.println();
      Serial1.flush();
    }
}