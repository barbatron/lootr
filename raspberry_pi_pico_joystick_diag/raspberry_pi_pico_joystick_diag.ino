#include <Arduino.h>

// Suggested KY-023 wiring for Pico diagnostics:
// VRx -> GP26 (ADC0), VRy -> GP27 (ADC1), SW -> GP15
static const uint8_t PIN_JOY_X = 26;
static const uint8_t PIN_JOY_Y = 27;
static const uint8_t PIN_JOY_SW = 15;

int centerX = 2048;
int centerY = 2048;

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

void calibrateCenter() {
  long sx = 0;
  long sy = 0;
  const int samples = 64;
  for (int i = 0; i < samples; i++) {
    sx += analogRead(PIN_JOY_X);
    sy += analogRead(PIN_JOY_Y);
    delay(2);
  }
  centerX = (int)(sx / samples);
  centerY = (int)(sy / samples);
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  c = recalibrate center (leave stick untouched)");
  Serial.println("  h = help");
}

void setup() {
  analogReadResolution(12);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);

  Serial.begin(115200);
  delay(350);

  Serial.println();
  Serial.println("=== Pico Joystick Diagnostics ===");
  Serial.println("Expected wiring:");
  Serial.println("  KY-023 GND -> Pico GND");
  Serial.println("  KY-023 +5V -> Pico 3V3");
  Serial.println("  KY-023 VRx -> GP26 (ADC0)");
  Serial.println("  KY-023 VRy -> GP27 (ADC1)");
  Serial.println("  KY-023 SW  -> GP15");
  Serial.println();

  calibrateCenter();
  Serial.print("Center calibrated: X=");
  Serial.print(centerX);
  Serial.print(" Y=");
  Serial.println(centerY);
  printHelp();
  Serial.println();
}

void loop() {
  static uint32_t last = 0;
  uint32_t now = millis();

  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'c' || c == 'C') {
      calibrateCenter();
      Serial.print("Recalibrated center: X=");
      Serial.print(centerX);
      Serial.print(" Y=");
      Serial.println(centerY);
    } else if (c == 'h' || c == 'H') {
      printHelp();
    }
  }

  if (now - last >= 100) {
    int xRaw = analogRead(PIN_JOY_X);
    int yRaw = analogRead(PIN_JOY_Y);
    bool pressed = (digitalRead(PIN_JOY_SW) == LOW);

    float x = clampf((float)(xRaw - centerX) / 2048.0f, -1.0f, 1.0f);
    float y = clampf((float)(yRaw - centerY) / 2048.0f, -1.0f, 1.0f);

    Serial.print("rawX=");
    Serial.print(xRaw);
    Serial.print(" rawY=");
    Serial.print(yRaw);
    Serial.print(" normX=");
    Serial.print(x, 3);
    Serial.print(" normY=");
    Serial.print(y, 3);
    Serial.print(" sw=");
    Serial.println(pressed ? "PRESSED" : "released");

    last = now;
  }
}
