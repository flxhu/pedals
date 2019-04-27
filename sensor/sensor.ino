#include <SPI.h>
#include <Wire.h>
#include <RF24.h>
#include <printf.h>

#define CSN_PIN 10
#define CE_PIN 9
#define RADIO_LEVEL RF24_PA_LOW

RF24 radio(CE_PIN, CSN_PIN);

void setup() {
  Serial.begin(9600);
  Serial.println("Start");

  byte address[6] = {"MyHub"};
  radio.begin();
  radio.setPALevel(RADIO_LEVEL);
  radio.setPayloadSize(5);
  radio.setCRCLength(RF24_CRC_16);
  radio.openWritingPipe(address);
  printf_begin();
  radio.printDetails();
  delay(500);
}

byte buffer[5] = {"123"};

void loop() {
  int hall = analogRead(A0);
  buffer[1] = (uint16_t)hall >> 8;
  buffer[2] = (uint16_t)hall & 0xff;
  radio.write(buffer, 5);
  
  // delay(1);
}
