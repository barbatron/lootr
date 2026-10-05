#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <I2S.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSD.h"

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
static const uint8_t PIN_I2S_LRCLK = 11;
static const uint8_t PIN_I2S_DOUT = 12; // Pico -> DIN on amp
static const int8_t PIN_AMP_SD = 13;    // optional; set -1 if hardwired high

#if defined(LED_BUILTIN)
static const int8_t PIN_STATUS_LED = LED_BUILTIN;
#else
static const int8_t PIN_STATUS_LED = -1;
#endif

// Playback / selection constants
static const long I2S_SAMPLE_RATE = 22050;
static const uint16_t PLAY_INTERVAL_MS = 170;
static const uint16_t TRIGGER_DEBOUNCE_MS = 20;

static const float DEADZONE = 0.06f;
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
int wavFilesOnCard = 0;
int groupCounts[GROUP_COUNT] = {0};

int centerX = 2048;
int centerY = 2048;
unsigned long lastPlayMs = 0;
unsigned long nextPlayMs = 0;
int lastPlayedIndex = -1;
bool triggerRawPressed = false;
bool triggerPressedStable = false;
bool wasTriggerPressedStable = false;
unsigned long triggerRawChangedMs = 0;
uint32_t triggerAttempts = 0;
uint32_t triggerStarted = 0;
uint32_t triggerStolen = 0;
uint32_t triggerDropped = 0;
uint32_t mixerFlushes = 0;
uint32_t mixerStarveEvents = 0;
uint64_t mixerSilencePadBytes = 0;

bool verboseEvents = false;
bool infoLogsEnabled = false;

uint32_t perfWindowStartUs = 0;
uint32_t perfLoopCount = 0;
uint64_t perfLoopBusyUs = 0;
uint32_t perfLoopMaxUs = 0;
uint64_t perfAudioBusyUs = 0;
uint32_t perfAudioMaxUs = 0;

// Audio configurations (Ensure all your WAV files match these settings)
const uint32_t SAMPLE_RATE = I2S_SAMPLE_RATE;
const uint8_t CHANNELS = 1;         // Mono saves considerable RAM/processing on Pico
const uint8_t BITS_PER_SAMPLE = 16;
const uint16_t PLAYER_BUFFER_BYTES = 256;

#define MAX_VOICES 1 // Monophonic debug mode: newest trigger replaces current voice

// Core Physical Audio Output
I2SStream out;                                // Final physical output
OutputMixer<int16_t> mixer(out, MAX_VOICES);  // Mixer feeding I2S, one slot per voice

struct MixerChannelOutput : public Print {
  int channel = 0;
  explicit MixerChannelOutput(int c) : channel(c) {}

  size_t write(uint8_t) override { return 0; }

  size_t write(const uint8_t* buffer, size_t size) override {
    return mixer.write(channel, buffer, size);
  }

  int availableForWrite() override {
    return mixer.availableForWrite(channel);
  }
};

// Structure representing a single polyphonic voice channel
struct AudioVoice {
  WAVDecoder decoder;
  AudioSourceSD* source = nullptr;
  MixerChannelOutput* output = nullptr;
  AudioPlayer* player = nullptr;
  bool isPlaying = false;
  uint32_t startedAtMs = 0;
};

AudioVoice voices[MAX_VOICES];
bool soundReady = false;

static const uint16_t STATUS_SHORT_MS = 95;
static const uint16_t STATUS_LONG_MS = 300;
static const uint16_t STATUS_GAP_MS = 95;

static void playStatusChirp(uint16_t freqHz, uint16_t durationMs, float amplitude = 0.16f) {
  if (!soundReady || freqHz == 0 || durationMs == 0) {
    delay(durationMs);
    return;
  }

  const float twoPi = 6.28318530718f;
  float gain = amplitude;
  if (gain < 0.0f) gain = 0.0f;
  if (gain > 1.0f) gain = 1.0f;

  const uint32_t totalSamples = (uint32_t)((I2S_SAMPLE_RATE * durationMs) / 1000UL);
  const float phaseStep = (twoPi * (float)freqHz) / (float)I2S_SAMPLE_RATE;
  float phase = 0.0f;

  for (uint32_t i = 0; i < totalSamples; i++) {
    int16_t s = (int16_t)(sinf(phase) * 32767.0f * gain);
    out.write((const uint8_t*)&s, sizeof(s));

    phase += phaseStep;
    if (phase > twoPi) {
      phase -= twoPi;
    }
  }
}

static void runStatusStep(uint16_t onMs, uint16_t offMs, uint16_t chirpFreqHz) {
  if (PIN_STATUS_LED >= 0) {
    digitalWrite(PIN_STATUS_LED, HIGH);
  }

  playStatusChirp(chirpFreqHz, onMs);

  if (PIN_STATUS_LED >= 0) {
    digitalWrite(PIN_STATUS_LED, LOW);
  }

  if (offMs > 0) {
    delay(offMs);
  }
}

static void signalBootBeforeSdScan() {
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 880);
}

static void signalSdScanComplete() {
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 880);
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 1175);
}

static void signalSdScanFailed() {
  runStatusStep(STATUS_LONG_MS, STATUS_GAP_MS, 440);
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 660);
}

static void signalSoundChipFailedOnce() {
  runStatusStep(STATUS_LONG_MS, STATUS_GAP_MS, 0);
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 0);
  runStatusStep(STATUS_SHORT_MS, STATUS_GAP_MS, 0);
}

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

static bool hasWavExtension(const char* name) {
  size_t n = strlen(name);
  if (n < 4) return false;
  return (tolower(name[n - 4]) == '.' &&
          tolower(name[n - 3]) == 'w' &&
          tolower(name[n - 2]) == 'a' &&
          tolower(name[n - 1]) == 'v');
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
  wavFilesOnCard = 0;
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
    if (!n) {
      entry.close();
      continue;
    }

    if (hasRawExtension(n)) {
      rawFilesOnCard++;
    }

    if (!hasWavExtension(n)) {
      entry.close();
      continue;
    }

    wavFilesOnCard++;

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

static int activeVoiceCount() {
  int count = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].isPlaying) count++;
  }
  return count;
}

static void setAudioToolsLogLevel(AudioToolsLogLevel level) {
  AudioToolsLogger.begin(Serial, level);
  infoLogsEnabled = (level == AudioToolsLogLevel::Info);
}

static void resetPerfMetrics() {
  perfWindowStartUs = micros();
  perfLoopCount = 0;
  perfLoopBusyUs = 0;
  perfLoopMaxUs = 0;
  perfAudioBusyUs = 0;
  perfAudioMaxUs = 0;
}

static void updatePerfMetrics(uint32_t loopUs, uint32_t audioUs) {
  perfLoopCount++;
  perfLoopBusyUs += loopUs;
  perfAudioBusyUs += audioUs;
  if (loopUs > perfLoopMaxUs) perfLoopMaxUs = loopUs;
  if (audioUs > perfAudioMaxUs) perfAudioMaxUs = audioUs;
}

static bool playRawSnippetMixed(const char* primaryFilename,
                                const char* transferFilename,
                                float materialGain,
                                float transferGain) {
  (void)transferFilename;
  (void)transferGain;

  int target = -1;
  bool stealing = false;

  // Find an available (idle) voice slot
  for (int i = 0; i < MAX_VOICES; i++) {
    if (!voices[i].isPlaying) {
      target = i;
      break;
    }
  }

  if (target < 0) {
    // Voice stealing: reuse the latest started channel.
    uint32_t newestStart = 0;
    for (int i = 0; i < MAX_VOICES; i++) {
      if (voices[i].isPlaying && voices[i].startedAtMs >= newestStart) {
        newestStart = voices[i].startedAtMs;
        target = i;
      }
    }
    if (target >= 0) {
      stealing = true;
      voices[target].player->stop();
      voices[target].player->clearBuffers(true);
      voices[target].isPlaying = false;
      voices[target].startedAtMs = 0;
    }
  }

  if (target < 0) {
    triggerDropped++;
    if (verboseEvents) {
      Serial.println("No available channels and no steal candidate.");
    }
    return false;
  }

  if (stealing && verboseEvents) {
    Serial.print("Stealing channel "); Serial.print(target);
    Serial.print(" for "); Serial.println(primaryFilename);
  } else if (verboseEvents) {
    Serial.print("Triggering "); Serial.print(primaryFilename); 
    Serial.print(" on channel: "); Serial.println(target);
  }
  if (stealing) {
    triggerStolen++;
  }

  voices[target].player->setVolume(materialGain);
  if (!voices[target].player->setPath(primaryFilename)) {
    Serial.print("ERROR: Failed to open WAV: ");
    Serial.println(primaryFilename);
    triggerDropped++;
    return false;
  }
  voices[target].isPlaying = true;
  voices[target].startedAtMs = millis();
  voices[target].player->play();
  triggerStarted++;
  return true;
}

static void printHelp() {
  Serial.println("Commands:");
  Serial.println("  h = help");
  Serial.println("  c = recalibrate joystick center");
  Serial.println("  r = rescan SD assets");
  Serial.println("  p = play one short test chirp");
  Serial.println("  v = toggle verbose trigger logs");
  Serial.println("  l = toggle AudioTools warning/info logs");
  Serial.println("  z = reset perf counters");
  Serial.println("  s = print status");
}

static void printStatus() {
  uint32_t elapsedUs = micros() - perfWindowStartUs;
  uint32_t loopAvgUs = (perfLoopCount > 0) ? (uint32_t)(perfLoopBusyUs / perfLoopCount) : 0;
  uint32_t audioAvgUs = (perfLoopCount > 0) ? (uint32_t)(perfAudioBusyUs / perfLoopCount) : 0;
  float busyPct = (elapsedUs > 0) ? ((float)perfLoopBusyUs * 100.0f / (float)elapsedUs) : 0.0f;
  float audioPct = (elapsedUs > 0) ? ((float)perfAudioBusyUs * 100.0f / (float)elapsedUs) : 0.0f;

  Serial.print("assetsIndexed=");
  Serial.print(assetCount);
  Serial.print(" wavOnCard=");
  Serial.print(wavFilesOnCard);
  Serial.print(" rawOnCard=");
  Serial.print(rawFilesOnCard);
  Serial.print(" dropped=");
  Serial.print(wavFilesOnCard - assetCount);
  Serial.print(" centerX=");
  Serial.print(centerX);
  Serial.print(" centerY=");
  Serial.print(centerY);
  Serial.print(" activeVoices=");
  Serial.print(activeVoiceCount());
  Serial.print(" trigAttempts=");
  Serial.print(triggerAttempts);
  Serial.print(" trigStarted=");
  Serial.print(triggerStarted);
  Serial.print(" trigStolen=");
  Serial.print(triggerStolen);
  Serial.print(" trigDropped=");
  Serial.print(triggerDropped);
  Serial.print(" mixerFlushes=");
  Serial.print(mixerFlushes);
  Serial.print(" starveEvents=");
  Serial.print(mixerStarveEvents);
  Serial.print(" silencePadBytes=");
  Serial.print((unsigned long)mixerSilencePadBytes);
  Serial.print(" perfBusy=");
  Serial.print(busyPct, 1);
  Serial.print("% perfAudio=");
  Serial.print(audioPct, 1);
  Serial.print("% loopAvgUs=");
  Serial.print(loopAvgUs);
  Serial.print(" loopMaxUs=");
  Serial.print(perfLoopMaxUs);
  Serial.print(" audioAvgUs=");
  Serial.print(audioAvgUs);
  Serial.print(" audioMaxUs=");
  Serial.print(perfAudioMaxUs);
  Serial.print(" verbose=");
  Serial.print(verboseEvents ? "on" : "off");
  Serial.print(" atLog=");
  Serial.print(infoLogsEnabled ? "info" : "warning");
  Serial.print(" trigRaw=");
  Serial.print(triggerRawPressed ? "on" : "off");
  Serial.print(" trigStable=");
  Serial.println(triggerPressedStable ? "on" : "off");
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
  triggerRawPressed = (digitalRead(PIN_JOY_SW) == LOW);
  triggerPressedStable = triggerRawPressed;
  wasTriggerPressedStable = triggerPressedStable;
  triggerRawChangedMs = millis();
  if (PIN_STATUS_LED >= 0) {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
  }

  if (PIN_AMP_SD >= 0) {
    pinMode(PIN_AMP_SD, OUTPUT);
    digitalWrite(PIN_AMP_SD, HIGH);
  }

  setAudioToolsLogLevel(AudioToolsLogLevel::Warning);
  AudioInfo info(SAMPLE_RATE, CHANNELS, BITS_PER_SAMPLE);

  // Configure physical I2S output
  auto config_out = out.defaultConfig(TX_MODE);
  config_out.copyFrom(info);
  config_out.pin_bck = PIN_I2S_BCLK;
  config_out.pin_ws = PIN_I2S_LRCLK;
  config_out.pin_data = PIN_I2S_DOUT;
  if (!out.begin(config_out)) {
    Serial.println("ERROR: AudioTools I2S output init failed");
    while (true) {
      signalSoundChipFailedOnce();
      delay(450);
    }
  }
  mixer.setAutoIndex(false);
  mixer.begin(4096);

  // Initialize the Voice Channels
  for (int i = 0; i < MAX_VOICES; i++) {
    voices[i].source = new AudioSourceSD("/", ".wav", PIN_SD_CS);
    voices[i].source->setAutoNext(false);
    voices[i].output = new MixerChannelOutput(i);
    voices[i].player = new AudioPlayer(*voices[i].source, (Print&)(*voices[i].output), voices[i].decoder);
    voices[i].player->setAutoNext(false);
    voices[i].player->setAutoFade(false);
    voices[i].player->setDelayIfOutputFull(0);
    voices[i].player->setBufferSize(PLAYER_BUFFER_BYTES);
    voices[i].player->setVolume(1.0);
  }  
  soundReady = true;

  // Optional soft start silence to avoid boot pops.
  // drainI2SForSilence(50);

  SPI.setRX(PIN_SD_MISO);
  SPI.setTX(PIN_SD_MOSI);
  SPI.setSCK(PIN_SD_SCK);
  SPI.begin();

  signalBootBeforeSdScan();

  if (!SD.begin(PIN_SD_CS)) {
    Serial.println("ERROR: SD init failed");
    signalSdScanFailed();
    while (true) delay(1000);
  }

  bool scanOk = scanAssets();
  if (!scanOk) {
    Serial.println("ERROR: asset scan failed");
    signalSdScanFailed();
  } else {
    signalSdScanComplete();
  }
  if (wavFilesOnCard > assetCount) {
    Serial.print("WARN: asset cap reached. Increase MAX_ASSETS to index all WAV files. dropped=");
    Serial.println(wavFilesOnCard - assetCount);
  }

  calibrateJoystickCenter();
  randomSeed((uint32_t)analogRead(PIN_JOY_X) ^ ((uint32_t)analogRead(PIN_JOY_Y) << 10));
  nextPlayMs = millis();

  Serial.println("Ready.");
  resetPerfMetrics();
  printStatus();
  printHelp();

  // Immediate audible confirmation path.
  if (assetCount > 0) {
    int transferIdx = pickTransferAssetIndex(0);
    const char* transferName = (transferIdx >= 0 && transferIdx != 0) ? assets[transferIdx].name : nullptr;
    playRawSnippetMixed(assets[0].name, transferName, 0.95f, 0.25f);
  }
}

void loop() {
  uint32_t loopStartUs = micros();

  // Pump any playing audio
  uint32_t audioStartUs = micros();
  size_t producedBytes[MAX_VOICES] = {0};
  size_t maxProducedBytes = 0;
  for (int i = 0; i < MAX_VOICES; i++) {
    if (voices[i].isPlaying) {
      size_t bytesRead = voices[i].player->copy();
      producedBytes[i] = bytesRead;
      if (bytesRead > maxProducedBytes) {
        maxProducedBytes = bytesRead;
      }
      if (bytesRead == 0 && !voices[i].player->isActive()) {
        voices[i].isPlaying = false;
        voices[i].startedAtMs = 0;
      }
    }
  }

  if (maxProducedBytes > 0) {
    for (int i = 0; i < MAX_VOICES; i++) {
      if (producedBytes[i] < maxProducedBytes) {
        size_t padBytes = maxProducedBytes - producedBytes[i];
        mixer.writeSilence(i, padBytes);
        mixerSilencePadBytes += padBytes;
      }
    }
    mixer.flushMixer();
    mixerFlushes++;
  } else if (activeVoiceCount() > 0) {
    mixerStarveEvents++;
  }
  uint32_t audioDurationUs = micros() - audioStartUs;

  // User input
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
      if (ok && wavFilesOnCard > assetCount) {
        Serial.print("WARN: asset cap reached. dropped=");
        Serial.println(wavFilesOnCard - assetCount);
      }
      printStatus();
    } else if (c == 'p' || c == 'P') {
      playStatusChirp(1175, 120, 0.18f);
    } else if (c == 'v' || c == 'V') {
      verboseEvents = !verboseEvents;
      Serial.print("verbose trigger logs: ");
      Serial.println(verboseEvents ? "on" : "off");
    } else if (c == 'l' || c == 'L') {
      setAudioToolsLogLevel(infoLogsEnabled ? AudioToolsLogLevel::Warning : AudioToolsLogLevel::Info);
      Serial.print("AudioTools logger level: ");
      Serial.println(infoLogsEnabled ? "info" : "warning");
    } else if (c == 'z' || c == 'Z') {
      resetPerfMetrics();
      mixerFlushes = 0;
      mixerStarveEvents = 0;
      mixerSilencePadBytes = 0;
      Serial.println("performance counters reset");
    } else if (c == 's' || c == 'S') {
      printStatus();
    }
  }

  // Read inputs
  int xRaw = analogRead(PIN_JOY_X);
  int yRaw = analogRead(PIN_JOY_Y);
  bool triggerPressedNow = (digitalRead(PIN_JOY_SW) == LOW);
  if (triggerPressedNow != triggerRawPressed) {
    triggerRawPressed = triggerPressedNow;
    triggerRawChangedMs = millis();
  }
  if ((millis() - triggerRawChangedMs) >= TRIGGER_DEBOUNCE_MS) {
    triggerPressedStable = triggerRawPressed;
  }

  float x = clampf((float)(xRaw - centerX) / 2048.0f, -1.0f, 1.0f);
  float y = clampf((float)(yRaw - centerY) / 2048.0f, -1.0f, 1.0f);
  float amp = sqrtf(x * x + y * y);
  float ampForSpread = (amp > DEADZONE) ? amp : 0.0f;
  float angleDeg = fmodf(degrees(atan2f(y, x)) + 360.0f, 360.0f);

  float spread = SPREAD_AT_CENTER - clampf(ampForSpread, 0.0f, 1.0f) * (SPREAD_AT_CENTER - SPREAD_AT_EDGE);

  unsigned long now = millis();
  bool justPressed = triggerPressedStable && !wasTriggerPressedStable;
  if (justPressed) {
    nextPlayMs = now;
  } else if (!triggerPressedStable) {
    nextPlayMs = now + PLAY_INTERVAL_MS;
  }

  if (triggerPressedStable && assetCount > 0 &&
      (int32_t)(now - nextPlayMs) >= 0) {
    triggerAttempts++;
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

      if (verboseEvents) {
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
      }

      if (playRawSnippetMixed(assets[idx].name, transferName, materialGain, transferGain)) {
        lastPlayedIndex = idx;
        lastPlayMs = now;
      }
    } else {
      triggerDropped++;
    }

    nextPlayMs += PLAY_INTERVAL_MS;
    if ((int32_t)(now - nextPlayMs) >= 0) {
      nextPlayMs = now + PLAY_INTERVAL_MS;
    }
  }

  wasTriggerPressedStable = triggerPressedStable;

  uint32_t loopDurationUs = micros() - loopStartUs;
  updatePerfMetrics(loopDurationUs, audioDurationUs);
}
