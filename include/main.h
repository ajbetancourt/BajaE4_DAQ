#ifndef main_h
#define main_h

void createBinFile();
void recordBinFile();
void renameBinFile();
void startRecording();
void displayWebPageCode(void * parameter);
void recordDataCode(void * parameter);
bool connectWifi();
void transferFileNames();
bool waitForACK(uint32_t timeout);
bool getCMD(String& incomingCmd,uint32_t timeout);
void sendCmd(String cmd, boolean addEOL,bool printCMD);
void binaryToCsv(char* fileName);
void checkButtons(int butLeft, int butRight);

#endif