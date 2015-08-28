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
#include <float.h>  // For DBL_MAX
#include "ESP8266HttpRead.h"

// (we use the default constructor for ESP8266HttpRead)

/*
 * Begin reading an Http response from the given ESP8266 Http Client.
 * Call this function after sending the Http command and before calling ::read().
 * 
 * esp8266Client = the ESP8266Client you are using to contact the web server.
 * timeoutMs = time (milliseconds) that each read() will wait for a response.
 * 
 * Returns true.
 */
boolean ESP8266HttpRead::begin(ESP8266Client& esp8266Client, unsigned long timeoutMs) {
  _pEsp8266Client = &esp8266Client;
  _timeoutMs = timeoutMs;
  
  _cmdState = CMD_WAIT;
  _nextIn = 0;
  _nextOut = 0;

  return true;
}

/*
 * Read the next byte from the Http response read by the ESP8266,
 * skipping ESP8266 commands that appear in the response.
 * 
 * Returns:
 *   >= 0 = the next byte from the Http response
 *   ESP8266HttpRead::READ_CLOSED = connection has been closed (0,CLOSED from the ESP8266).
 *   ESP8266HttpRead::READ_TIMEOUT = timeout occurred before a byte was received.
 *   ESP8266HttpRead::READ_ERROR = an error occurred.  Most likely, the caller didn't call ::begin().
 * 
 * This function is necessary because the ESP8266 inserts
 * communication about the data transfer into the data transfer itself.
 * For example, the string "\n+IPD,0,1475:" can appear anywhere in the data
 * and the string "0,CLOSED" appears at the end.
 */
int ESP8266HttpRead::read() {
  if (!_pEsp8266Client) {
    return READ_ERROR;     // begin() wasn't called first.
  }
  
  unsigned long startMillis = millis();
  while (true) {

    // If we're flushing _cmdBuf[], return the next character to flush.
    if (_nextOut < _nextIn) {
      return _cmdBuf[_nextOut++];
    }

    // If we're not in the middle of a command, reset _cmdBuf[]
    if (_cmdState == CMD_WAIT) {
      _nextIn = 0;
      _nextOut = _nextIn;
    }

    // wait for data until it appears or we run out of time.
    while (!_pEsp8266Client->available()) {
      if (millis() - startMillis > _timeoutMs) {
        return READ_TIMEOUT;
      }
      delay(1);
    }
    
    _cmdBuf[_nextIn] = _pEsp8266Client->read();
    // return _cmdBuf[_nextIn];  // DEBUG to see the raw data the board returns.

    /*
     * Recognize and skip the following commands from ESP8266:
     * \n+IPD,...: = command that says more data is available.
     * 0,CLOSED = the connection to the server has been closed.
     * 
     * This is a state machine: the current state (CMD_*) and the input character
     * together determine the new state.
     */
     
    switch (_cmdState) {
    case CMD_WAIT:
      if ((char) _cmdBuf[_nextIn] == '\n') {
        ++_nextIn;
        _nextOut = _nextIn;
        _cmdState = CMD_NL;

      } else if ((char) _cmdBuf[_nextIn] == '0') {
        ++_nextIn;
        _nextOut = _nextIn;
        _cmdState = CMD_0;
        
      } else {
        ++_nextIn;
        _nextOut = 0;
        _cmdState = CMD_WAIT;
      }
      break;
    case CMD_NL: advanceIf('+', CMD_PLUS); break;
    case CMD_PLUS: advanceIf('I', CMD_I); break;
    case CMD_I: advanceIf('P', CMD_P); break;
    case CMD_P: advanceIf('D', CMD_D); break;
    case CMD_D: advanceIf(',', CMD_COMMA); break;
    case CMD_COMMA:
      // absorb characters until a :
      if ((char) _cmdBuf[_nextIn] != ':') {
        ++_nextIn;
        _nextOut = _nextIn;
        _cmdState = CMD_COMMA;
        break;
      }

      // We've seen \n+IPD,...:  Skip that string.
      _nextIn = 0;
      _nextOut = 0;
      _cmdState = CMD_WAIT;
      break;

    case CMD_0: advanceIf(',', CMD_0_); break;
    case CMD_0_: advanceIf('C', CMD_0_C); break;
    case CMD_0_C: advanceIf('L', CMD_0_CL); break;
    case CMD_0_CL: advanceIf('O', CMD_0_CLO); break;
    case CMD_0_CLO: advanceIf('S', CMD_0_CLOS); break;
    case CMD_0_CLOS: advanceIf('E', CMD_0_CLOSE); break;
    case CMD_0_CLOSE:
      if ((char) _cmdBuf[_nextIn] == 'D') {
        /*
         * We've received the 0,CLOSED message.
         * The ESP8266 has finished sending data from the server.
         */
        return READ_CLOSED;
        
      } else {
        ++_nextIn;
        _nextOut = 0;
        _cmdState = CMD_WAIT;
      }
      break;
 
    default:
      return READ_CLOSED;
    }

  }
  
}

/*
 * Calls ::read() of the given count of characters
 * and copies the characters into buf[].
 */
boolean ESP8266HttpRead::read(char *buf, short count) {
  char *pNext = buf;
  char *pEnd = &buf[count];

  while (pNext < pEnd) {
    int ch = read();
    if (ch < 0) {
      return false;
    }
    *pNext++ = (char) ch;
  }
  return true;
}

/*
 * Like Serial.find(), but uses our ::read() instead of read().
 */
boolean ESP8266HttpRead::find(char *ppattern) {
  char *p = ppattern;
  int ch;

  while (*p != '\0') {
    ch = read();
    if (ch < 0) {
      return false;
    }
    if ((char) ch == *p) {
      ++p;
    } else {
      p = ppattern; // we assume no internal repetition in the input.  E.g., no "DaDate: ".
    }
  }
  return true;
}

/*
 * Skips to the "Date:" Http header
 * then parse the date header, through the timezone.
 * The Timezone must be GMT
 * Return true if successful, false otherwise.
 * 
 * Example date header returned in the HTTP response from a web server:
 * Date: Fri, 21 Aug 2015 22:06:40 GMT
 * 
 * To use:
 *   ESP8266HttpRead::HttpDateTime dateTime;
 *   ...
 *   reader.findDate(&dateTime);
 *   Serial.print(dateTime.year);
 */
boolean ESP8266HttpRead::findDate(struct HttpDateTime *pDateTimeUTC) {
  char buf[4];

  pDateTimeUTC->daySinceSunday = -1;
  pDateTimeUTC->year = -1;
  pDateTimeUTC->month = -1;
  pDateTimeUTC->day = -1;
  pDateTimeUTC->hour = -1;
  pDateTimeUTC->minute = -1;
  pDateTimeUTC->second = -1;
 
  if (!find("Date: ")) {
    return false;
  }

  // Day of week: Sun Mon Tue Wed Thu Fri Sat
  if (!read(buf, 3)) {
    return false;
  }
  if (buf[0] == 'S') {          // Sun or Sat
    if (buf[1] == 'u') {        // Sun
      pDateTimeUTC->daySinceSunday = 0;
    } else if (buf[1] == 'a') { // Sat
      pDateTimeUTC->daySinceSunday = 6;
    } else { // garbled day of week.
      return false;
    }
  } else if (buf[0] == 'M') {   // Mon
    pDateTimeUTC->daySinceSunday = 1;
  } else if (buf[0] == 'T') {   // Tue or Thu
    if (buf[1] == 'u') {        // Tue
      pDateTimeUTC->daySinceSunday = 2;
    } else if (buf[1] == 'h') { // Thu
      pDateTimeUTC->daySinceSunday = 4;
    } else {  // garbled day of week.
      return false;
    }
  } else if (buf[0] == 'W') {   // Wed
    pDateTimeUTC->daySinceSunday = 3;
  } else if (buf[0] == 'F') {   // Fri
    pDateTimeUTC->daySinceSunday = 5;
  } else { // garbled day of week.
    return false;
  }

  // Skip the ", " after the day of the week.
  if (!read(buf, 2)) {
    return false;
  }

  // Day of the month: 1..31
  if (!read(buf, 2)) {
    return false;
  }
  if (!('0' <= buf[0] && buf[0] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[1] && buf[1] <= '9')) {
    return false;  // garbled
  }
  pDateTimeUTC->day = (buf[0] - '0') * 10 + (buf[1] - '0');

  // Skip the space before the month.
  if (read() < 0) {
    return false;
  }

  // Month: Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec
  if (!read(buf, 3)) {
    return false; // garbled.
  }
  if (buf[0] == 'J') {          // Jan, Jun, or Jul
    if (buf[1] == 'a') {        // Jan
      pDateTimeUTC->month = 1;
    } else if (buf[2] == 'n') { // Jun
      pDateTimeUTC->month = 6;
    } else if (buf[2] == 'l') { // Jul
      pDateTimeUTC->month = 7;
    } else {
      return false; // garbled
    }
  } else if (buf[0] == 'F') {   // Feb
    pDateTimeUTC->month = 2;
  } else if (buf[0] == 'M') {   // Mar or May
    if (buf[2] == 'r') {        // Mar
      pDateTimeUTC->month = 3;
    } else if (buf[2] == 'y') { // May
      pDateTimeUTC->month = 5;
    } else {
      return false; // garbled
    }
  } else if (buf[0] == 'A') {   // Apr or Aug
    if (buf[1] == 'p') {        // Apr
      pDateTimeUTC->month = 4;
    } else if (buf[1] == 'u') { // Aug
      pDateTimeUTC->month = 8;
    } else {
      return false;
    }
  } else if (buf[0] == 'S') {   // Sep
    pDateTimeUTC->month = 9;
  } else if (buf[0] == 'O') {   // Oct
    pDateTimeUTC->month = 10;
  } else if (buf[0] == 'N') {   // Nov
    pDateTimeUTC->month = 11;
  } else if (buf[0] == 'D') {   // Dec
    pDateTimeUTC->month = 12;
  } else {
    return false; // garbled
  }

  // Skip the space before the year
  if (read() < 0) {
    return false;
  }

  // Year: 1900..2100 or so.
  if (!read(buf, 4)) {
    return false;
  }
  if (!('0' <= buf[0] && buf[0] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[1] && buf[1] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[2] && buf[2] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[3] && buf[3] <= '9')) {
    return false;  // garbled
  }
  pDateTimeUTC->year = (buf[0] - '0') * 1000
    + (buf[1] - '0') * 100
    + (buf[2] - '0') * 10
    + (buf[3] - '0');

  // Skip the space before the hour
  if (read() < 0) {
    return false;
  }

  // Hour: 00..23
  if (!read(buf, 2)) {
    return false;
  }
  if (!('0' <= buf[0] && buf[0] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[1] && buf[1] <= '9')) {
    return false;  // garbled
  }
  pDateTimeUTC->hour = (buf[0] - '0') * 10 + (buf[1] - '0');

  // Skip the : before the minute
  if (read() < 0) {
    return false;
  }

  // Minute: 00..59
  if (!read(buf, 2)) {
    return false;
  }
  if (!('0' <= buf[0] && buf[0] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[1] && buf[1] <= '9')) {
    return false;  // garbled
  }
  pDateTimeUTC->minute = (buf[0] - '0') * 10 + (buf[1] - '0');

  // Skip the : before the second
  if (read() < 0) {
    return false;
  }

  // Second: 00..61 (usually 00..59)
  if (!read(buf, 2)) {
    return false;
  }
  if (!('0' <= buf[0] && buf[0] <= '9')) {
    return false;  // garbled
  }
  if (!('0' <= buf[1] && buf[1] <= '9')) {
    return false;  // garbled
  }
  pDateTimeUTC->second = (buf[0] - '0') * 10 + (buf[1] - '0');

  // Skip the space before the Timezone
  if (read() < 0) {
    return false;
  }

  // Timezone: GMT hopefully.
  if (!read(buf, 3)) {
    return false;
  }
  if (buf[0] != 'G' || buf[1] != 'M' || buf[2] != 'T') {
    // buf[3] = '\0';  // so we can print it.
    // Serial.print(F("TZ not GMT: "));
    // Serial.println(buf);
    return false;    // Timezone not GMT
  }

  return true;
}

/*
 * Read a double-floating-point value from the input,
 * and the character just past that double.
 * For example "11.9X" would return 11.9 and would read
 * the X character following the string "11.9"
 * Note: there must be at least one character following the number.
 * That is, the input mustn't end immediately after the number.
 * 
 * Accepts unsigned decimal numbers such as
 * 34
 * 15.
 * 90.54
 * .2
 *
 * Returns either the decimal number, or DBL_MAX (see <float.h>) if an error occurs.
 */
double ESP8266HttpRead::readDouble() {
  int ch;

  double result = 0.0;

  // Read the integer part of the number (if there is one)

  boolean sawInteger = false;
  ch = read();
  while ('0' <= (char) ch && (char) ch <= '9') {
    sawInteger = true;
    result *= 10.0;
    result += (char) ch - '0';

    ch = read();
  }
  if (ch < 0) {
    return DBL_MAX;    // early end of file or error.
  }
  if (ch != '.') {
    if (!sawInteger) {
      return DBL_MAX;   // no number was found at all.
    }
    return result;
  }

  // read the fractional part of the number (if there is one)

  double scale = 0.1;
  ch = read();
  while ('0' <= (char) ch && (char) ch <= '9') {
    sawInteger = true;
    result += scale * ((char) ch - '0');
    scale /= 10.0;

    ch = read();
  }
  if (ch < 0) {
    return DBL_MAX;
  }
  if (!sawInteger) {
    return DBL_MAX;
  }

  return result;
}

/*
 * Call this after a ::read() has returned -1.
 */
void ESP8266HttpRead::end() {
  _pEsp8266Client = 0;
}

/*
 * Part of the command recognition state machine.
 * If the given character has been received, advance to the given state.
 * Otherwise, flush the buffer contents and wait for a command.
 */
void ESP8266HttpRead::advanceIf(char wantChar, byte newState) {
  if ((char) _cmdBuf[_nextIn] == wantChar) {
    ++_nextIn;
    _nextOut = _nextIn;
    _cmdState = newState;
    return;
  }

  // Not found. Reset the search.

  ++_nextIn;
  _nextOut = 0;
  _cmdState = CMD_WAIT;

}
