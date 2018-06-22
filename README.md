# mqtt_doorbell

This repository contains sample code and eagle/gerber files for the MQTT doorbell board I sell on tindie: https://www.tindie.com/products/ErikLemcke/mqtt--wifi-doorbell-with-esp8266/ 

This board that can transfer your ordinary doorbell (running on 8 - 24v ac) to a wifi doorbell. The board works with an ESP12 ESP8266 module that you can program with a FTDI module and (for example) the Arduino IDE. Whenever someone presses your doorbell, the board can execute an action, while leaving the regular doorbell circuit intact, so that if your module crashes for one or another reason, your doorbell will still be working. The assembled version can be used without any programming to send MQTT messages, for example to Home Assistant.

To program the board, make sure you provide it with power, plug in a ftdi module and set the dipswitch to on.

I use the folowing settings for programming:

- Board: Generic EP8266 module
- Flash mode: QIO
Flash size: 512K (64K SPIFFS)
Debugging port: Disabled
Debug level: None
IwIP variant: V2 lower memory
reset method: ck
crystal frequency: 26Mhz
Flash frequency: 40Mhz
CPU frequency: 80Mhz
Builtin led: 2
Upload speed: 115200
Erase flash: Only sketch
