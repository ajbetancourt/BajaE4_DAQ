#ifndef SERIAL_CMDS_H
#define SERIAL_CMDS_H


#define ACK "ACK" 
#define NAK "NAK"
#define SRT "SRT"
#define END "END" //End of transmission
#define RDY "RDY" 
#define EOL '\0'  //End of line
#define FNAME "FNAME" //Signifies that the incoming data is the file name
#define FDATA "FDATA" //Signifies that the incoming data is the file data
#define DEL "DEL" //Delete file
#define ERR "ERR" //Error with requested operation

#endif