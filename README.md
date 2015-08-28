# ESP8266HttpRead
Arduino library to filter ESP8266 WiFi Shield messages from web server responses.

Sparkfun offers an [ESP8266 WiFi Shield](https://www.sparkfun.com/products/13287) and an [Arduino library](https://github.com/sparkfun/SparkFun_ESP8266_AT_Arduino_Library) to communicate with it.

When reading an HTTP Response using the Shield and current (August 2015) version of the library, the data your Arduino Sketch reads includes two types of (unwanted) messages from the Shield that appear in the output. For example,
```
\n+IPD,0,1475:
```
is a message from the Shield saying that 1475 bytes follow this message.  These +IPD messages can appear anywhere in the response from a web site.  Also,
```
0,CLOSED
```
appears when the web server closes the connection.

The ESP8266HttpRead library is designed to remove these messages from the response sent by a web site.  The library also has a few handy functions for processing the response from a web site.

See ESP8266HttpRead.h for notes on how to use the library.
