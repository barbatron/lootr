#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <I2S.h>
#include <math.h>
#include <string.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Pico Lootr Integration Diagnostics
// Combines: joystick input + SD asset scan + MAX98357A I2S playback.
// ---------------------------------------------------------------------------

// SD reader (SPI0)
static const uint8_t PIN_SD_CS = 17;
static const uint8_t PIN_SD_SCK = 18;
static const uint8_t PIN_SD_MOSI = 19;
static const uint8_t PIN_SD_MISO = 16;

// Joystick (KY-023)
static const uint8_t PIN_JOY_X = 26;  // ADC0
static const uint8_t PIN_JOY_Y = 27;  // ADC1
static const uint8_t PIN_JOY_SW = 15; // active LOW

// MAX98357A (I2S)
static const uint8_t PIN_I2S_BCLK = 10;
static const uint8_t PIN_I2S_DOUT = 12; // Pico -> DIN on amp
static const int8_t PIN_AMP_SD = 13;    // optional; set -1 if hardwired high

// Playback / selection constants
static const long I2S_SAMPLE_RATE = 44100;
static const uint16_t PLAY_INTERVAL_MS = 150;
static const uint16_t MAX_PLAY_MS = 220; // per trigger snippet length
static const float DEADZONE = 0.05f;
static const float SPREAD_AT_CENTER = 180.0f;
static const float SPREAD_AT_EDGE = 20.0f;

static const int MAX_ASSETS = 320;

struct AssetEntry {
  char name[48];
  float angle;
};

AssetEntry assets[MAX_ASSETS];
int assetCount = 0;
int rawFilesOnCard = 0;

int centerX = 2048;
int centerY = 2048;
unsigned long lastPlayMs = 0;
int lastPlayedIndex = -1;

I2S i2s(OUTPUT);

static float clampf(float v, float lo, float hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

static float angularDistance(float a, float b) {
  float diff = fabsf(fmodf(a - b, 360.0f));
  return fminf(diff, 360.0f - diff);
}

static bool hasRawExtension(const char* name) {
  size_t n = strlen(name);
  if (n < 4) return false;
  return (tolower(name[n - 4]) == '.' &&
          tolower(name[n - 3]) == 'r' &&
          tolower(name[n - 2]) == 'a' &&
          tolower(name[n - 1]) == 'w');
}

static bool containsToken(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != nullptr;
}

static float angleForName(const char* lowerName) {
  // 270 = up (metal/mechanical)
  if (containsToken(lowerName, "metal") || containsToken(lowerName, "can") ||
      containsToken(lowerName, "gun") || containsToken(lowerName, "pipe") ||
      containsToken(lowerName, "blade") || containsToken(lowerName, "wire")) {
    return 270.0f;
  }

  // 180 = left (minerals/earth)
  if (containsToken(lowerName, "charcoal") || containsToken(lowerName, "charcol") ||
      containsToken(lowerName, "sulfur") || containsToken(lowerName, "sulphur") ||
      containsToken(lowerName, "stone") || containsToken(lowerName, "ore") ||
      containsToken(lowerName, "coal")) {
    return 180.0f;
  }

  // 0 = right (wood)
  if (containsToken(lowerName, "wood") || containsToken(lowerName, "plank") ||
      containsToken(lowerName, "stick") || containsToken(lowerName, "log")) {
    return 0.0f;
  }

  // fallback = 90 (down)
  return 90.0f;
}

static void toLowerAscii(char* s) {
  for (; *s; ++s) {
    *s = (char)tolower(*s);
  }
}

static void calibrateJoystickCenter() {
  long sx = 0;
  long sy = 0;
  const int samples = 80;
  for (int i = 0; i < samples; i++) {
    sx += analogRead(PIN_JOY_X);
    sy += analogRead(PIN_JOY_Y);
    delay(2);
  }
  centerX = (int)(sx / samples);
  centerY = (int)(sy / samples);
}

static bool scanAssets() {
  assetCount = 0;
  rawFilesOnCard = 0;

  File root = SD.open("/");
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return false;
  }

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    if (entry.isDirectory()) {
      entry.close();
      continue;
    }

    const char* n = entry.name();
    if (!n || !hasRawExtension(n)) {
      entry.close();
      continue;
    }

    rawFilesOnCard++;

    if (assetCount >= MAX_ASSETS) {
      entry.close();
      continue;
    }

    strncpy(assets[assetCount].name, n, sizeof(assets[assetCount].name) - 1);
    assets[assetCount].name[sizeof(assets[assetCount].name) - 1] = '\0';

    char lowerName[48];
    strncpy(lowerName, assets[assetCount].name, sizeof(lowerName) - 1);
    lowerName[sizeof(lowerName) - 1] = '\0';
    toLowerAscii(lowerName);
    assets[assetCount].angle = angleForName(lowerName);

    assetCount++;
    entry.close();
  }

  root.close();
  return true;
}

static int pickAssetIndex(float inputAngleDeg, float maxSpreadDeg) {
  if (assetCount <= 0) return -1;

  float weights[MAX_ASSETS];
  int indices[MAX_ASSETS];
  int candidateCount = 0;
  float totalWeight = 0.0f;

  for (int i = 0; i < assetCount; i++) {
    float dist = angularDistance(inputAngleDeg, assets[i].angle);
    if (dist <= maxSpreadDeg) {
      float w = maxSpreadDeg - dist;
      w = w * w;
      if (w > 0.0f) {
        weights[candidateCount] = w;
        indices[candidateCount] = i;
        totalWeight += w;
        candidateCount++;
      }
    }
  }

  if (candidateCount == 0 || totalWeight <= 0.0f) {
    // Fallback to nearest asset by target angle, randomized across ties.
    int nearest[MAX_ASSETS];
    int nearestCount = 0;
    float bestDist = 9999.0f;
    const float tieEps = 0.01f;

    for (int i = 0; i < assetCount; i++) {
      float d = angularDistance(inputAngleDeg, assets[i].angle);
      if (d + tieEps < bestDist) {
        bestDist = d;
        nearestCount = 0;
        nearest[nearestCount++] = i;
      } else if (fabsf(d - bestDist) <= tieEps) {
        nearest[nearestCount++] = i;
      }
    }

    if (nearestCount <= 0) return -1;

    int pick = nearest[random(0, nearestCount)];
    if (nearestCount > 1 && pick == lastPlayedIndex) {
      // Nudge away from exact immediate repeats when alternatives exist.
      int pick2 = nearest[random(0, nearestCount)];
      if (pick2 != pick) pick = pick2;
    }
    return pick;
  }

  float r = ((float)random(0, 10000) / 10000.0f) * totalWeight;
  float accum = 0.0f;
  int selected = indices[candidateCount - 1];
  for (int i = 0; i < candidateCount; i++) {
    accum += weights[i];
    if (r <= accum) {
      selected = indices[i];
      break;
    }
  }

  if (candidateCount > 1 && selected == lastPlayedIndex) {
    // One weighted re-roll to reduce obvious repetition at fixed stick positions.
    float r2 = ((float)random(0, 10000) / 10000.0f) * totalWeight;
    float accum2 = 0.0f;
    for (int i = 0; i < candidateCount; i++) {
      accum2 += weights[i];
      if (r2 <= accum2 && indices[i] != selected) {
        selected = indices[i];
        break;
      }
    }
  }

  return selected;
}

static void drainI2SForSilence(uint32_t ms) {
  uint32_t totalSamples = (I2S_SAMPLE_RATE * ms) / 1000UL;
  for (uint32_t i = 0; i < totalSamples; i++) {
    i2s.write16(0, 0);
  }
}

static bool playRawSnippet(const char* filename, uint16_t maxMs) {
  File f = SD.open(filename, FILE_READ);
  if (!f) {
    Serial.print("open failed: ");
    Serial.println(filename);
    return false;
  }

  const uint32_t maxBytes = (uint32_t)maxMs * (I2S_SAMPLE_RATE * 2UL) / 1000UL;
  // Mono 16-bit input => 2 bytes/sample.
  uint32_t playedBytes = 0;

  static uint8_t buf[512];
  while (f.available() && playedBytes < maxBytes) {
    int toRead = sizeof(buf);
    if (maxBytes - playedBytes < (uint32_t)toRead) {
      toRead = (int)(maxBytes - playedBytes);
    }

    int n = f.read(buf, toRead);
    if (n <= 0) break;

    // Ensure even count for 16-bit sample reads.
    if (n & 1) n--;

    for (int i = 0; i < n; i += 2) {
      int16_t s = (int16_t)((uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8));
      i2s.write16(s, s); // duplicate mono into L/R
    }

    playedBytes += (uint32_t)n;
  }

  f.close();
  i2s.flush();
  return true;
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  h = help");
  Serial.println("  c = recalibrate joystick center");
  Serial.println("  r = rescan SD assets");
  Serial.println("  p = play one short test chirp");
  Serial.println("  s = print status");
}

static void printStatus() {
  Serial.print("assetsIndexed=");
  Serial.print(assetCount);
  Serial.print(" rawOnCard=");
  Serial.print(rawFilesOnCard);
  Serial.print(" dropped=");
  Serial.print(rawFilesOnCard - assetCount);
  Serial.print(" centerX=");
  Serial.print(centerX);
  Serial.print(" centerY=");
  Serial.println(centerY);
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("=== Pico Lootr Integration ===");
  Serial.println("SD + joystick + MAX98357A playback test");
  Serial.println("Requires Earle Philhower RP2040 core.");

  analogReadResolution(12);
  pinMode(PIN_JOY_SW, INPUT_PULLUP);

  if (PIN_AMP_SD >= 0) {
    pinMode(PIN_AMP_SD, OUTPUT);
    digitalWrite(PIN_AMP_SD, HIGH);
  }

  if (!i2s.setBCLK(PIN_I2S_BCLK) || !i2s.setDATA(PIN_I2S_DOUT) ||
      !i2s.setBitsPerSample(16) || !i2s.begin(I2S_SAMPLE_RATE)) {
    Serial.println("ERROR: I2S init failed");
    while (true) delay(1000);
  }

  // Optional soft start silence to avoid boot pops.
  drainI2SForSilence(50);

  SPI.setRX(PIN_SD_MISO);
  SPI.setTX(PIN_SD_MOSI);
  SPI.setSCK(PIN_SD_SCK);
  SPI.begin();

  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("ERROR: SD init failed");
    while (true) delay(1000);
  }

  if (!scanAssets()) {
    Serial.println("ERROR: asset scan failed");
  }
  if (rawFilesOnCard > assetCount) {
    Serial.print("WARN: asset cap reached. Increase MAX_ASSETS to index all files. dropped=");
    Serial.println(rawFilesOnCard - assetCount);
  }

  calibrateJoystickCenter();
  randomSeed((uint32_t)analogRead(PIN_JOY_X) ^ ((uint32_t)analogRead(PIN_JOY_Y) << 10));

  Serial.println("Ready.");
  printStatus();
  printHelp();

  // Immediate audible confirmation path.
  if (assetCount > 0) {
    playRawSnippet(assets[0].name, 120);
  }
}

void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'h' || c == 'H') {
      printHelp();
    } else if (c == 'c' || c == 'C') {
      calibrateJoystickCenter();
      Serial.println("recalibrated");
      printStatus();
    } else if (c == 'r' || c == 'R') {
      bool ok = scanAssets();
      Serial.println(ok ? "rescan ok" : "rescan failed");
      if (ok && rawFilesOnCard > assetCount) {
        Serial.print("WARN: asset cap reached. dropped=");
        Serial.println(rawFilesOnCard - assetCount);
      }
      printStatus();
    } else if (c == 'p' || c == 'P') {
      if (assetCount > 0) playRawSnippet(assets[0].name, 120);
    } else if (c == 's' || c == 'S') {
      printStatus();
    }
  }

  int xRaw = analogRead(PIN_JOY_X);
  int yRaw = analogRead(PIN_JOY_Y);
  bool triggerPressed = (digitalRead(PIN_JOY_SW) == LOW);

  float x = clampf((float)(xRaw - centerX) / 2048.0f, -1.0f, 1.0f);
  float y = clampf((float)(yRaw - centerY) / 2048.0f, -1.0f, 1.0f);
  float amp = sqrtf(x * x + y * y);
  float angleDeg = fmodf(degrees(atan2f(y, x)) + 360.0f, 360.0f);

  float spread = SPREAD_AT_CENTER - clampf(amp, 0.0f, 1.0f) * (SPREAD_AT_CENTER - SPREAD_AT_EDGE);

  unsigned long now = millis();
  if (triggerPressed && amp > DEADZONE && (now - lastPlayMs) >= PLAY_INTERVAL_MS && assetCount > 0) {
    int idx = pickAssetIndex(angleDeg, spread);
    if (idx >= 0) {
      Serial.print("play: ");
      Serial.print(assets[idx].name);
      Serial.print(" angle=");
      Serial.print(angleDeg, 1);
      Serial.print(" amp=");
      Serial.print(amp, 2);
      Serial.print(" spread=");
      Serial.print(spread, 1);
      Serial.print(" target=");
      Serial.println(assets[idx].angle, 1);

      playRawSnippet(assets[idx].name, MAX_PLAY_MS);
      lastPlayedIndex = idx;
      lastPlayMs = now;
    }
  }
}
