# Helicopter Joysticks (Collective and Pedal) for flight simulators

Aerofly FS 2 sports a Robinson R22 model, which is evidently to realistic enough to get acquire some heli flying skills. 

Putting the collective on the throttle on the flight stick and using sticky Logitech pedals turned out to be not high quality enough to do decent flying. As it seemed a fun thing to do (and I didn't do Arduino for a while), it of course became a DIY project.

The project consists of three things:
1. a mechanical design of a collective stick, using only standard parts (no tools or 3D printer required),
2. a mechanical design of pedals, again only using standard parts,
3. electronics for measuring stick sensors, transmitting the data over 2.4 GHz, along with a hub that presents itself as a normal USB joystick.

# Collective Stick

BOM and pictures soon.

# Pedals 

A mostly tool-less construction. Aluminium pipes connected with a module connector system.

![Padels](pedals_1.jpg)

# Current Electronics (ESP32 as Joystick via Bluetooth BLE) in esp32_ble_stick/

BOM:
* LOLIN32 Lite
* CJMCU-103 Rotary Angle Sensor Modul Murata SV01A

# Old Electronics (in hub/ and sensor/)

BOM USB joystick hub:
* Arduino Pro Micro 5V (has USB)
* nRF24L01+ SPI module (takes 3.3V, hence the next item)
* NRF24L01+ PCB Adapter (5V to 3.3V)
* SSD1306 128x64 display module

BOM wireless sensor board:
* Arduino Pro Mini 3.3V / 8 MHz  // ATTENTION: mine has an ATmega168, which is relevant when programming
* nRF24L01+ SPI module (takes 3.3V, hence the Arduino)
* 3.3V hall sensor connected to Analog In Pin A0
* Switch connected to Digital In Pin 2
* 2xAA battery holder
* Mini Step-Up/Step-Down Schaltregler 1.8V-5V zu 3.3V

BOM and schematics soon.

You'll find the code for the sensor and the hub in the respective directories. Use the Arduino IDE to compile them and load them to your Arduino board.
