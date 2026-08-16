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
static const uint16_t PLAY_INTERVAL_MS = 160;
static const uint16_t MAX_PLAY_MS = 220; // per trigger snippet length
static const float DEADZONE = 0.01f;
static const float SPREAD_AT_CENTER = 180.0f;
static const float SPREAD_AT_EDGE = 15.0f;

static const char* TRANSFER_LAYER_TOKEN = "cloth";
static const float MATERIAL_GAIN_MIN = 0.90f;
static const float MATERIAL_GAIN_MAX = 1.00f;
static const float TRANSFER_GAIN_MIN = 0.20f;
static const float TRANSFER_GAIN_MAX = 0.40f;

static const int MAX_ASSETS = 320;

enum MaterialGroup : uint8_t {
  GROUP_OTHER = 0,
  GROUP_METAL = 1,
  GROUP_ORE = 2,
  GROUP_WOOD = 3,
  GROUP_PLASTIC = 4,
  GROUP_WATER = 5,
  GROUP_CLOTH = 6,
  GROUP_COUNT = 7,
};

struct AssetEntry {
  char name[48];
  uint8_t group;
  float angle;
};

AssetEntry assets[MAX_ASSETS];
int assetCount = 0;
int rawFilesOnCard = 0;
int groupCounts[GROUP_COUNT] = {0};

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

static float randf(float lo, float hi) {
  float t = (float)random(0, 10000) / 10000.0f;
  return lo + t * (hi - lo);
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

static uint8_t groupForName(const char* lowerName) {
  if (containsToken(lowerName, "cloth")) return GROUP_CLOTH;
  if (containsToken(lowerName, "liquid-container")) return GROUP_WATER;
  if (containsToken(lowerName, "plastic")) return GROUP_PLASTIC;

  // Match Python grouping behavior for implicit metal aliases.
  if (containsToken(lowerName, "metal") || containsToken(lowerName, "can") ||
      containsToken(lowerName, "chainlink") || containsToken(lowerName, "spring") ||
      containsToken(lowerName, "gun") || containsToken(lowerName, "pipe") ||
      containsToken(lowerName, "blade") || containsToken(lowerName, "wire")) {
    return GROUP_METAL;
  }

  if (containsToken(lowerName, "charcoal") || containsToken(lowerName, "charcol") ||
      containsToken(lowerName, "sulfur") || containsToken(lowerName, "sulphur") ||
      containsToken(lowerName, "stone") || containsToken(lowerName, "ore") ||
      containsToken(lowerName, "coal")) {
    return GROUP_ORE;
  }

  if (containsToken(lowerName, "wood") || containsToken(lowerName, "plank") ||
      containsToken(lowerName, "stick") || containsToken(lowerName, "log")) {
    return GROUP_WOOD;
  }

  return GROUP_OTHER;
}

static float angleForGroup(uint8_t group) {
  if (group == GROUP_METAL) return 270.0f;
  if (group == GROUP_ORE) return 180.0f;
  if (group == GROUP_WOOD) return 0.0f;
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
  for (int i = 0; i < GROUP_COUNT; i++) groupCounts[i] = 0;

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
    assets[assetCount].group = groupForName(lowerName);
    assets[assetCount].angle = angleForGroup(assets[assetCount].group);
    groupCounts[assets[assetCount].group]++;

    assetCount++;
    entry.close();
  }

  root.close();
  return true;
}

static int pickGroupForAngle(float inputAngleDeg, float maxSpreadDeg) {
  float weights[GROUP_COUNT];
  int groups[GROUP_COUNT];
  int candidateCount = 0;
  float totalWeight = 0.0f;

  for (int g = 0; g < GROUP_COUNT; g++) {
    if (groupCounts[g] <= 0) continue;
    float dist = angularDistance(inputAngleDeg, angleForGroup((uint8_t)g));
    if (dist <= maxSpreadDeg) {
      float w = maxSpreadDeg - dist;
      w = w * w;
      if (w > 0.0f) {
        weights[candidateCount] = w;
        groups[candidateCount] = g;
        totalWeight += w;
        candidateCount++;
      }
    }
  }

  if (candidateCount == 0 || totalWeight <= 0.0f) {
    // Fallback to nearest group by target angle.
    int nearest[GROUP_COUNT];
    int nearestCount = 0;
    float bestDist = 9999.0f;
    const float tieEps = 0.01f;

    for (int g = 0; g < GROUP_COUNT; g++) {
      if (groupCounts[g] <= 0) continue;
      float d = angularDistance(inputAngleDeg, angleForGroup((uint8_t)g));
      if (d + tieEps < bestDist) {
        bestDist = d;
        nearestCount = 0;
        nearest[nearestCount++] = g;
      } else if (fabsf(d - bestDist) <= tieEps) {
        nearest[nearestCount++] = g;
      }
    }

    if (nearestCount <= 0) return -1;
    return nearest[random(0, nearestCount)];
  }

  float r = ((float)random(0, 10000) / 10000.0f) * totalWeight;
  float accum = 0.0f;
  for (int i = 0; i < candidateCount; i++) {
    accum += weights[i];
    if (r <= accum) return groups[i];
  }
  return groups[candidateCount - 1];
}

static int pickAssetInGroup(uint8_t group, int avoidIndex) {
  int candidates[MAX_ASSETS];
  int count = 0;

  for (int i = 0; i < assetCount; i++) {
    if (assets[i].group == group) {
      candidates[count++] = i;
    }
  }
  if (count <= 0) return -1;

  int pick = candidates[random(0, count)];
  if (count > 1 && pick == avoidIndex) {
    int pick2 = candidates[random(0, count)];
    if (pick2 != pick) pick = pick2;
  }
  return pick;
}

static int pickTransferAssetIndex(int avoidIndex) {
  int transfer = pickAssetInGroup(GROUP_CLOTH, avoidIndex);
  return transfer;
}

static void drainI2SForSilence(uint32_t ms) {
  uint32_t totalSamples = (I2S_SAMPLE_RATE * ms) / 1000UL;
  for (uint32_t i = 0; i < totalSamples; i++) {
    i2s.write16(0, 0);
  }
}

static bool playRawSnippetMixed(const char* primaryFilename,
                                const char* transferFilename,
                                uint16_t maxMs,
                                float materialGain,
                                float transferGain) {
  File material = SD.open(primaryFilename, FILE_READ);
  if (!material) {
    Serial.print("open failed: ");
    Serial.println(primaryFilename);
    return false;
  }

  File transfer;
  bool hasTransfer = false;
  if (transferFilename && transferFilename[0] != '\0') {
    transfer = SD.open(transferFilename, FILE_READ);
    hasTransfer = (bool)transfer;
  }

  const uint32_t maxBytes = (uint32_t)maxMs * (I2S_SAMPLE_RATE * 2UL) / 1000UL;
  // Mono 16-bit input => 2 bytes/sample.
  uint32_t playedBytes = 0;

  static uint8_t materialBuf[512];
  static uint8_t transferBuf[512];
  while (material.available() && playedBytes < maxBytes) {
    int toRead = sizeof(materialBuf);
    if (maxBytes - playedBytes < (uint32_t)toRead) {
      toRead = (int)(maxBytes - playedBytes);
    }

    int nMat = material.read(materialBuf, toRead);
    if (nMat <= 0) break;

    int nTr = 0;
    if (hasTransfer) {
      nTr = transfer.read(transferBuf, toRead);
      if (nTr < 0) nTr = 0;
    }

    // Ensure even count for 16-bit sample reads.
    if (nMat & 1) nMat--;
    if (nTr & 1) nTr--;

    for (int i = 0; i < nMat; i += 2) {
      int16_t sMat = (int16_t)((uint16_t)materialBuf[i] | ((uint16_t)materialBuf[i + 1] << 8));
      int16_t sTr = 0;
      if (hasTransfer && i < nTr) {
        sTr = (int16_t)((uint16_t)transferBuf[i] | ((uint16_t)transferBuf[i + 1] << 8));
      }

      float mixed = (float)sMat * materialGain + (float)sTr * transferGain;
      if (mixed > 32767.0f) mixed = 32767.0f;
      if (mixed < -32768.0f) mixed = -32768.0f;

      int16_t out = (int16_t)mixed;
      i2s.write16(out, out); // duplicate mono into L/R
    }

    playedBytes += (uint32_t)nMat;
  }

  material.close();
  if (hasTransfer) transfer.close();
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
    int transferIdx = pickTransferAssetIndex(0);
    const char* transferName = (transferIdx >= 0 && transferIdx != 0) ? assets[transferIdx].name : nullptr;
    playRawSnippetMixed(assets[0].name, transferName, 120, 0.95f, 0.25f);
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
      if (assetCount > 0) {
        int transferIdx = pickTransferAssetIndex(0);
        const char* transferName = (transferIdx >= 0 && transferIdx != 0) ? assets[transferIdx].name : nullptr;
        playRawSnippetMixed(assets[0].name, transferName, 120, 0.95f, 0.25f);
      }
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
    int chosenGroup = pickGroupForAngle(angleDeg, spread);
    int idx = -1;
    if (chosenGroup >= 0) {
      idx = pickAssetInGroup((uint8_t)chosenGroup, lastPlayedIndex);
    }
    if (idx >= 0) {
      int transferIdx = -1;
      if (assets[idx].group != GROUP_CLOTH) {
        transferIdx = pickTransferAssetIndex(idx);
      }
      const char* transferName = (transferIdx >= 0) ? assets[transferIdx].name : nullptr;
      float materialGain = randf(MATERIAL_GAIN_MIN, MATERIAL_GAIN_MAX);
      float transferGain = (transferName != nullptr) ? randf(TRANSFER_GAIN_MIN, TRANSFER_GAIN_MAX) : 0.0f;

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

      playRawSnippetMixed(assets[idx].name, transferName, MAX_PLAY_MS, materialGain, transferGain);
      lastPlayedIndex = idx;
      lastPlayMs = now;
    }
  }
}
