#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RF24.h>
#include <HID-Project.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     4 // Reset pin # (or -1 if sharing Arduino reset pin)

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define CSN_PIN 10
#define CE_PIN 9
#define RADIO_LEVEL RF24_PA_MIN

RF24 radio(CE_PIN, CSN_PIN);

void setup() {
  byte address[6] = {"MyHub"};
  radio.begin();
  radio.setPALevel(RADIO_LEVEL);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(5);
  radio.openReadingPipe(1, address);
  radio.startListening();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.begin(9600);
    Serial.println(F("SSD1306 allocation failed"));
    for(;;); // Don't proceed, loop forever
  }
  display.clearDisplay();

  display.setTextSize(1);      // Normal 1:1 pixel scale
  display.setTextColor(WHITE); // Draw white text
  display.setCursor(0, 0);     // Start at top-left corner
  display.cp437(true);         // Use full 256 char 'Code Page 437' font

  display.print(F("Radio: "));
  if (radio.isValid()) {
    display.println(radio.isPVariant() ? " nRF24L01+" : " nRF24L01");
  } else {
    display.println("error");
  }
  display.print(F("Channel: "));
  display.println(radio.getChannel());
  display.print(F("Payload size: "));
  display.println(radio.getPayloadSize());
  display.print(F("PA level: "));
  display.println(PALevelToString(radio.getPALevel()));
  display.print(F("Data rate: "));
  display.println(DataRateToString(radio.getDataRate()));
  display.print(F("Carrier: "));
  display.println(radio.testRPD() ? "Good" : "Weak");

  display.display();

  // Sends a clean report to the host. This is important on any Arduino type.
  Gamepad.begin();
  delay(2000);
}

const char* PALevelToString(uint8_t level) {
  switch (level) {
    case 0: return "-18 dBm";
    case 1: return "-12 dBm";
    case 2: return "-6 dBm"; 
    case 3: return "0 dBm";  
  }
}

const char* DataRateToString(rf24_datarate_e rate) {
  switch (rate) {
    case RF24_1MBPS: return "1 Mbit";
    case RF24_2MBPS: return "2 Mbit";
    case RF24_250KBPS: return "250 Kbit";
  }
}

void renderValue(const char* name, int button, uint16_t hall, int16_t axis) {
  int16_t axisNorm = hall / 100;
  display.print(name);
  display.print(" ");
  display.println(button);
  for (int i = 0; i < axisNorm; ++i) {
    // display.print("#");
  }
  for (int i = 0; i < 10 - axisNorm; ++i) {
    // display.print(".");
  }  
  display.print(" . ");
  display.println(axis);
  display.println(hall);  
}

int16_t normalize(uint16_t value, int16_t min, int16_t max) {
  if (value < min) return -32767;
  if (value > max) return 32767;
  int16_t midPoint = max - min;
  int16_t factor = 32768 / (midPoint / 2);
  return (value - min - midPoint/2) * factor;
}

#define NUM_SENSORS 2

uint16_t sensorAnalogMin[NUM_SENSORS] = {300, 0};
uint16_t sensorAnalogMax[NUM_SENSORS] = {700, 1024};
uint16_t sensorAnalogRaw[NUM_SENSORS] = {0, 0};
uint16_t sensorAnalogNormalized[NUM_SENSORS] = {0, 0};
int button[NUM_SENSORS] = {0, 0};

int loopCount = 0;

void loop() {
  uint8_t pipeno;
  byte data[5];
  while (radio.available(&pipeno)) {                           
    radio.read(data, 5);

    uint16_t analogValue = ((uint16_t)data[1]) << 8 | (uint16_t)data[2];
    sensorAnalogRaw[data[0]] = analogValue;
    button[data[0]] = data[3];
  }

  for (int i = 0; i < NUM_SENSORS; ++i) {
    sensorAnalogNormalized[i] = 
        normalize(sensorAnalogRaw[i], sensorAnalogMin[i], sensorAnalogMax[i]);
  }
  
  Gamepad.yAxis(sensorAnalogNormalized[0]);
  Gamepad.xAxis(sensorAnalogNormalized[1]);
  Gamepad.write();
  
  if (loopCount++ % 200 != 0) {
    return;
  }
  
  display.clearDisplay();
  display.setCursor(0, 0);

  renderValue("Collective", button[0], sensorAnalogNormalized[0], sensorAnalogRaw[0]);
  renderValue("Pedal", button[1], sensorAnalogNormalized[1], sensorAnalogRaw[1]);
  
  if (radio.testRPD()) {
    display.setCursor(110, 0);     // Start at top-left corner
    display.write(0x12);
  } else {
    display.setCursor(110, 0);     // Start at top-left corner
    display.print("?");
  }
  display.display();
}
