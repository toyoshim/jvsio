# JVSIO

![C/C++ CI](https://github.com/toyoshim/chlib/actions/workflows/c-cpp.yml/badge.svg)

JVSIO is a general library to handle JVS - JAMMA Video Standard - Bus v3, and
JVS Dash protocols. It's written in C and provides APIs for C, C++, and Arduino.

## Built-in Supported Boards / Devices
 - Arduino Nano/Uno
 - Arduino Mega 2560
 - MightyCore
 - SparkFun Pro Micro (thanks to [@hnakai0909](https://github.com/hnakai0909))

 You can use the library with other devices by writing your own client class.
 See clients/ directory for more details. I already succeeded to make it work
 on several ATMEGA chips, CH559, and several ARM Cortex M0(+) based chips.

## Applications that uses this library

 - [iona](https://github.com/toyoshim/iona) for Arduino
 - [iona-346](https://github.com/toyoshim/iona-346) for Arduino + SEGA Saturn Pad
 - [iona-js](https://github.com/toyoshim/iona-js) for the original ATMEGA32A based board + JAMMA
 - [iona-us](https://github.com/toyoshim/iona-us) for CH559 + JAMMA / USB
 - [JvsIoTester](https://github.com/toyoshim/JvsIoTester) that supports the host mode to test JVS I/O boards
 - (more ... let me know!)
