#ifndef ESP8266HttpRead_h
#define ESP8266HttpRead_h

/*
 * Library for reading web server responses from the
 * Sparkfun ESP8266 WiFi Shield
 * https://www.sparkfun.com/products/13287
 * The library exists because the ESP8266 inserts transfer messages
 * into the received data.  This library removes those inserted messages.
 * See ESP8266HttpRead::read() for details.
 * 
 * Copyright (c) 2015 Bradford Needham
 * (@bneedhamia, https://www.needhamia.com)
 * Licensed under the LGPL version 3
 * a version of which should be supplied with this file.
 */

#include <Arduino.h>
#include <SoftwareSerial.h> 
#include <SparkFunESP8266WiFi.h>

/*
 * The object used to read data from the WiFi shield.
 * To use:
 *   ESP8266HttpRead reader;
 *   ...
 *   Connect to a web server and query it via the Sparkfun ESP8266 library, then
 *   reader.begin(...);
 *   ...reader.read();
 *   ...
 *   reader.end();
 */
class ESP8266HttpRead {
  private:
    ESP8266Client *_pEsp8266Client; // The underlying ESP8266 web client
    unsigned long _timeoutMs;       // timeout (milliseconds) per read() call.

    /*
     * CMD_* = state machine state for ESP8266 commands in the input stream.
     * Designed to recognize and skip "\n+IPD,.*:" and "0,CLOSED"
     */
    enum CmdState {
      CMD_WAIT,  // Waiting for a message from the ESP8266
      
      CMD_NL,    // newline (\n) has been received
      CMD_PLUS,  // \n+ has been received
      CMD_I,     // \n+I
      CMD_P,     // \n+IP
      CMD_D,     // \n+IPD
      CMD_COMMA, // \n+IPD,
      // then anything until a colon ends the command

      CMD_0,       // 0 has been received
      CMD_0_,      // 0,
      CMD_0_C,     // 0,C
      CMD_0_CL,    // 0,CL
      CMD_0_CLO,   // 0,CLO
      CMD_0_CLOS,  // 0,CLOS
      CMD_0_CLOSE  // 0,CLOSE
      // then a D to end the command
    };
    
    byte _cmdState;    // current state of the command-recognition state machine. See CMD_*
    
    int _cmdBuf[20];   // Buffer storing a string that might be a command, but might not.
    int _nextIn = 0;   // index of the next available space in _cmdBuf[]
    int _nextOut = 0;  // if != _nextIn, index of the next thing to flush from _cmdBuf[]

    void advanceIf(char wantChar, byte newState);
    
  public:
    /*
     * Return values from ::read().
     */
    const int READ_ERROR = -3;         // Error (didn't call begin() before readWithin())
    const int READ_TIMEOUT = -2;       // timeout passed before the byte was received.
    const int READ_CLOSED = -1;        // connection was closed.

    /*
     * The Date and Time returned from parseDate().
     * I would have used the C++ struct tm, but that didn't seem to be available in the Arduino library.
     * NOTE: some fields' values differ from the corresponding fields in struct tm.
     */
    struct HttpDateTime {
      short daySinceSunday; // 0..6 Sunday = 0; Monday = 1; Saturday = 6
      short year;           // 1900..2100
      short month;          // 1..12 January = 1
      short day;            // 1..31  Day of the month
      short hour;           // 0..23  Midnight = 0; Noon = 12
      short minute;         // 0..59
      short second;         // 0..61 (usually 0..59)
    };

    boolean begin(ESP8266Client& esp8266Client, unsigned long timeoutMs);
    void end();
    int read();
    boolean read(char *buf, short count);
    boolean find(char *ppattern);
    boolean findDate(struct HttpDateTime *pDateTimeUTC);
    double readDouble();
};

#endif // ESP8266HttpRead_h
