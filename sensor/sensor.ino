// Programming notes:
// - use Arduino Pro or Pro Mini / ATmega168 3.3V / 8 MHz

#include <SPI.h>
#include <Wire.h>
#include <RF24.h>
#include <printf.h>

#define SENSOR_NO 0  // 0 Collective, 1 Pedal

#define CSN_PIN 10
#define CE_PIN 9
#define RADIO_LEVEL RF24_PA_MAX
#define BUTTON_PIN 2
#define HALL_PIN A0

RF24 radio(CE_PIN, CSN_PIN);

void setup() {
  Serial.begin(9600);
  Serial.println("Start");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

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

byte buffer[5] = {SENSOR_NO, 0, 0, 0, 0};

void loop() {
  int button = digitalRead(BUTTON_PIN);
  int hall = analogRead(HALL_PIN);
  buffer[1] = (uint16_t)hall >> 8;
  buffer[2] = (uint16_t)hall & 0xff;
  buffer[3] = button;
  radio.write(buffer, 5);
}
