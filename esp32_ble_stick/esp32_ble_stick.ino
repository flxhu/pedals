/*
   Arduino IDE:
   WEMOS LOLIN32
*/

#include <ArduinoUniqueID.h>
#include <BleGamepad.h>
#include <WiFi.h>

const int WAKUP_TOUCH_PIN = T0;       // Touch pin for wake-up from sleep
const int LED_PIN = 22;               // WEMOS LOLIN32 LED
const int POTI_PIN = 34;              // Potentiometer is connected to GPIO 34 (Analog ADC1_CH6)

// Board id, to use one sketch for multiple boards
const char COLLECTIVE_ID[6] = {0x24, 0xA, 0xC4, 0x5A, 0xC3, 0x1C};
const char PEDAL_ID[6] = {0x7C, 0x9E, 0xBD, 0xED, 0x81, 0xDC};

const int numberOfPotSamples = 4;     // Number of pot samples to take (to smooth the values)
const int delayBetweenSamples = 1;    // Delay in milliseconds between pot samples
const int delayBetweenHIDReports = 5; // Additional delay in milliseconds between HID reports

const boolean DEBUG = false;

// Per-board variables
boolean configIsRudder = false;
boolean configIsThrottle = false;
int16_t configLowerValue;
int16_t configUpperValue;
adc_attenuation_t configAttenuation;

BleGamepad* bleGamepad;

int sampleClassic();
int sampleEMA();
void blink(int millis);
void  print_wakeup_reason();
void  print_wakeup_touchpad();

const char compile_date[] = __DATE__ " " __TIME__;

void callback(){
  //placeholder callback function
}

const char* getName() {
  if (strncmp((const char*)UniqueID, COLLECTIVE_ID, UniqueIDsize) == 0) {
    configIsThrottle = true;
    configLowerValue = 0;
    configUpperValue = 1024;
    configAttenuation = ADC_11db;
    return "Collective";
  } else if (strncmp((const char*)UniqueID, PEDAL_ID, UniqueIDsize) == 0) {
    configIsRudder = true;
    configLowerValue = 580;
    configUpperValue = 910;
    configAttenuation = ADC_0db;
    return "Seitenruder";
  } else {
    Serial.println("Unknown id:");
    Serial.println(UniqueIDsize);
    for(size_t i = 0; i < UniqueIDsize; i++) {
      Serial.print("0x");
      Serial.print(UniqueID[i], HEX);
      Serial.print(", ");
    }  
    esp_deep_sleep_start();
  }
}

const int64_t MICROS_IN_SECONDS = 1000000LL;
const int64_t MINUTES_IN_SECONDS = 60LL;

const int64_t WARN_AFTER_SECONDS = 13LL * MINUTES_IN_SECONDS;
const int64_t SLEEP_AFTER_SECONDS = 15LL * MINUTES_IN_SECONDS;  
const int WAKEUP_TOUCH_THRESHOLD = 5;

const int TOUCH_ACTIVE_IN_A_ROW = 10;

int64_t lastActivityTimestampMicros = 0;
int touchActiveCount = 0;

void setup() {
  WiFi.mode(WIFI_OFF);
  
  Serial.begin(115200);
  const char* name = getName();
  Serial.print("Device: ");
  Serial.print(name);
  Serial.print(", compiled ");
  Serial.println(compile_date);


  print_wakeup_reason();
  print_wakeup_touchpad();
  
  bleGamepad = new BleGamepad(name, "Felix Hupfeld");

  bleGamepad->setControllerType(CONTROLLER_TYPE_JOYSTICK);
  bleGamepad->begin(
      2, // button
      0, // hat switches
      false,  // enable x
      false, false, false, false, false, false, false,
      configIsRudder,
      configIsThrottle, 
      false, false, false);
    
  bleGamepad->setBatteryLevel(90);
 
  pinMode(LED_PIN, OUTPUT);
  
  analogSetAttenuation(configAttenuation);
  analogReadResolution(10);
  setCpuFrequencyMhz(80);

  lastActivityTimestampMicros = esp_timer_get_time();
}

const float MOVING_AVERAGE_THRESH = 10.0;
const int MOVING_AVERAGE_SAMPLES = 20;
int avgSamples[MOVING_AVERAGE_SAMPLES];
int nextAvgSampleIndex = 0;

boolean isSignificantlyDifferent(int value) {
  avgSamples[nextAvgSampleIndex] = value;
  nextAvgSampleIndex = (nextAvgSampleIndex + 1) % MOVING_AVERAGE_SAMPLES;

  float average = 0;
  for (int i = 0; i < MOVING_AVERAGE_SAMPLES; ++i) {
    average += avgSamples[i];
  }
  average = average / MOVING_AVERAGE_SAMPLES;
  if (DEBUG) {
    Serial.print(average);
    Serial.print(" < ");
    Serial.println(value);
  }
  boolean result = abs(average - value) > MOVING_AVERAGE_THRESH;
  if (result) {  
      Serial.print("Detected activity, average: ");
      Serial.print(average);
      Serial.print(" value: ");
      Serial.print(value);
      Serial.println();
  }
  return result;
}

void loop() {
  if (!bleGamepad->isConnected()) {
    Serial.println("Waiting for Bluetooth...");
    blink(50);
    delay(500);
    blink(50);
    delay(2000);
  } else {
    int potValue = sampleEMA();
    int adjustedValue = map(
        constrain(potValue, configLowerValue, configUpperValue), 
        configLowerValue, configUpperValue, 
        32737, -32737);
    
    if (configIsRudder) {
      bleGamepad->setRudder(adjustedValue);
    }
    if (configIsThrottle) {
      bleGamepad->setThrottle(adjustedValue);
    }

    if (isSignificantlyDifferent(potValue)) {
      lastActivityTimestampMicros = esp_timer_get_time();
    }
      
    if (DEBUG) {
      printValues(adjustedValue, potValue);
    }
  }
  const uint64_t inactivitySeconds = 
      (esp_timer_get_time() - lastActivityTimestampMicros) / MICROS_IN_SECONDS;

  boolean warn = false;
  if (inactivitySeconds > WARN_AFTER_SECONDS) {
    warn = true;
  }
  
  if (inactivitySeconds > SLEEP_AFTER_SECONDS) {
    Serial.println("Going to sleep now.");
    digitalWrite(LED_PIN, HIGH);
    
    esp_sleep_enable_touchpad_wakeup();
    touchAttachInterrupt(WAKUP_TOUCH_PIN, callback, WAKEUP_TOUCH_THRESHOLD);
    esp_deep_sleep_start();
  }

  digitalWrite(LED_PIN, LOW);
  delay(delayBetweenHIDReports);
  if (!warn) {
    digitalWrite(LED_PIN, HIGH);
  }
}

void blink(int millis) {
  digitalWrite(LED_PIN, LOW);
  delay(millis);
  digitalWrite(LED_PIN, HIGH);
}

int potValues[numberOfPotSamples];  // Array to store pot readings

float EMA_a = 0.65;
int EMA_S = 0;

int sampleEMA() {
  delay(50);
  int sensorValue = sampleClassic(); // analogRead(POTI_PIN);
  EMA_S = (EMA_a * sensorValue) + ((1 - EMA_a) * EMA_S);
  return EMA_S;
}

int sampleClassic() {
  int potValue = 0;   // Variable to store calculated pot reading average

  // Populate readings
  for (int i = 0 ; i < numberOfPotSamples ; i++)
  {
    potValues[i] = analogRead(POTI_PIN);
    delay(delayBetweenSamples);
  }

  // Iterate through the readings to sum the values
  for (int i = 0 ; i < numberOfPotSamples ; i++) {
    potValue += potValues[i];
  }

  // Calculate the average
  potValue = potValue / numberOfPotSamples;

  return potValue;
}

void printValues(int adjustedValue, int potValue) {
  // Print readings to serial port
  Serial.print("Sent: ");
  Serial.print(adjustedValue);
  Serial.print("\tRaw Avg: ");
  Serial.print(potValue);
  Serial.print("\tRaw: {");

  // Iterate through raw pot values, printing them to the serial port
  for (int i = 0 ; i < numberOfPotSamples ; i++) {
    Serial.print(potValues[i]);

    // Format the values into a comma seperated list
    if (i == numberOfPotSamples - 1) {
      Serial.println("}");
    } else {
      Serial.print(", ");
    }
  }
}

void print_wakeup_reason(){
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

void print_wakeup_touchpad(){
  touch_pad_t touchPin = esp_sleep_get_touchpad_wakeup_status();

  switch(touchPin)
  {
    case 0  : Serial.println("Touch detected on GPIO 4"); break;
    case 1  : Serial.println("Touch detected on GPIO 0"); break;
    case 2  : Serial.println("Touch detected on GPIO 2"); break;
    case 3  : Serial.println("Touch detected on GPIO 15"); break;
    case 4  : Serial.println("Touch detected on GPIO 13"); break;
    case 5  : Serial.println("Touch detected on GPIO 12"); break;
    case 6  : Serial.println("Touch detected on GPIO 14"); break;
    case 7  : Serial.println("Touch detected on GPIO 27"); break;
    case 8  : Serial.println("Touch detected on GPIO 33"); break;
    case 9  : Serial.println("Touch detected on GPIO 32"); break;
    default : Serial.println("Wakeup not by touchpad"); break;
  }
}
