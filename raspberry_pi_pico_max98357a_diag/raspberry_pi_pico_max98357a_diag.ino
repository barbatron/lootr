#include <Arduino.h>
#include <I2S.h>
#include <math.h>

// Raspberry Pi Pico + MAX98357A wiring used by this diagnostic sketch:
//   GP10 -> BCLK
//   GP11 -> LRC/WS (auto-assigned as BCLK+1 by the Arduino-Pico I2S PIO impl)
//   GP12 -> DIN
//   GP13 -> SD (optional amp enable/mute control)
//   VBUS -> Vin, GND -> GND, speaker -> SPK+ / SPK-
static const uint8_t PIN_I2S_BCLK = 10;
static const uint8_t PIN_I2S_DATA = 12;
static const int8_t PIN_AMP_SD = 13;  // Set to -1 if SD is hard-wired high.

static const long SAMPLE_RATE = 22050;
static const float DEFAULT_AMPLITUDE = 0.20f;  // Keep conservative to prevent clipping.

I2S i2s(OUTPUT);
bool ampEnabled = true;

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  h = help");
  Serial.println("  t = 3-tone test (440/660/880 Hz)");
  Serial.println("  s = sweep test (220 -> 1760 Hz)");
  Serial.println("  m = toggle amp SD pin (mute/unmute)");
}

static void setAmpEnabled(bool enable) {
  ampEnabled = enable;
  if (PIN_AMP_SD >= 0) {
    digitalWrite(PIN_AMP_SD, enable ? HIGH : LOW);
  }
  Serial.print("amp=");
  Serial.println(enable ? "enabled" : "muted");
}

static void playTone(float freqHz, uint32_t durationMs, float amplitude = DEFAULT_AMPLITUDE) {
  if (!ampEnabled) {
    return;
  }

  const uint32_t totalSamples = (uint32_t)((SAMPLE_RATE * durationMs) / 1000UL);
  const float phaseStep = (2.0f * PI * freqHz) / (float)SAMPLE_RATE;
  float phase = 0.0f;

  for (uint32_t i = 0; i < totalSamples; i++) {
    int16_t s = (int16_t)(sinf(phase) * 32767.0f * amplitude);
    i2s.write16(s, s);

    phase += phaseStep;
    if (phase > 2.0f * PI) {
      phase -= 2.0f * PI;
    }
  }
}

static void runThreeToneTest() {
  Serial.println("[AUDIO] three-tone test");
  playTone(440.0f, 350);
  playTone(660.0f, 350);
  playTone(880.0f, 350);
  i2s.flush();
}

static void runSweepTest() {
  Serial.println("[AUDIO] sweep test");
  const int steps = 18;
  for (int i = 0; i < steps; i++) {
    float t = (float)i / (float)(steps - 1);
    float f = 220.0f + t * (1760.0f - 220.0f);
    playTone(f, 90, 0.18f);
  }
  i2s.flush();
}

void setup() {
  Serial.begin(115200);
  delay(350);

  Serial.println();
  Serial.println("=== Pico MAX98357A Audio Diagnostics ===");
  Serial.println("Requires Earle Philhower Arduino-Pico core (not Arduino Mbed RP2040).");
  Serial.println("Expected wiring:");
  Serial.println("  BCLK -> GP10");
  Serial.println("  LRC  -> GP11");
  Serial.println("  DIN  -> GP12");
  Serial.println("  SD   -> GP13 (optional)");
  Serial.println("  Vin  -> VBUS(5V) or 3V3");
  Serial.println("  GND  -> GND");
  Serial.println();

  if (PIN_AMP_SD >= 0) {
    pinMode(PIN_AMP_SD, OUTPUT);
    digitalWrite(PIN_AMP_SD, HIGH);
  }

  if (!i2s.setBCLK(PIN_I2S_BCLK)) {
    Serial.println("ERROR: i2s.setBCLK failed");
    while (true) {
      delay(1000);
    }
  }

  if (!i2s.setDATA(PIN_I2S_DATA)) {
    Serial.println("ERROR: i2s.setDATA failed");
    while (true) {
      delay(1000);
    }
  }

  if (!i2s.setBitsPerSample(16)) {
    Serial.println("ERROR: i2s.setBitsPerSample failed");
    while (true) {
      delay(1000);
    }
  }

  i2s.setBuffers(4, 256);

  if (!i2s.begin(SAMPLE_RATE)) {
    Serial.println("ERROR: i2s.begin failed");
    while (true) {
      delay(1000);
    }
  }

  Serial.print("I2S started at ");
  Serial.print(SAMPLE_RATE);
  Serial.println(" Hz");

  printHelp();
  runThreeToneTest();
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'h' || c == 'H') {
      printHelp();
    } else if (c == 't' || c == 'T') {
      runThreeToneTest();
    } else if (c == 's' || c == 'S') {
      runSweepTest();
    } else if (c == 'm' || c == 'M') {
      setAmpEnabled(!ampEnabled);
    }
  }

  // Heartbeat chirp every 3s, useful if serial monitor is closed.
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last >= 3000) {
    playTone(523.25f, 80, 0.16f);  // C5 short chirp
    i2s.flush();
    last = now;
  }
}
