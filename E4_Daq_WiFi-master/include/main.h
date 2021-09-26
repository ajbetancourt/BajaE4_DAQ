#ifndef MAIN_H

#define MAIN_H
#include <Arduino.h>

void getNewFiles();
void handleRoot();
void sendFile();
bool getCMD(String& incomingCmd,int timeout);
bool waitForACK(int timeout);
void sendCmd(String cmd,boolean addEOL,bool printCMD);
void updateFileSelection();
int transferFileNames();
int transferFileData(String fileName);
int deleteFile();
void handleFileDelete();
void sendStatus(uint8_t statusCode);

#endif
