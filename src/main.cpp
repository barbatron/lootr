// Lootr — Loot-o-mo-tron
// Teensy 4.0 main sketch
//
// Ported from proto.py. See spec.md for full porting guide and wiring.
//
// Audio pipeline:
//   SD card (SPI) → AudioPlaySdRaw → AudioMixer4 → AudioOutputAnalog (default)
//   or AudioOutputUSB when USB_AUDIO_OUT is defined.

#include <Arduino.h>
#include <Audio.h>
#include <SD.h>
#include <Bounce2.h>
#include <math.h>
#include <string.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Audio objects (Teensy Audio Library)
// ---------------------------------------------------------------------------

AudioPlaySdRaw  playerMaterial0;
AudioPlaySdRaw  playerMaterial1;
AudioPlaySdRaw  playerTransfer;
AudioMixer4              mixer;

#ifdef USB_AUDIO_OUT
AudioOutputUSB           audioOut;
AudioConnection patchCord0(playerMaterial0, 0, mixer,    0);
AudioConnection patchCord1(playerMaterial1, 0, mixer,    1);
AudioConnection patchCord2(playerTransfer,   0, mixer,    2);
AudioConnection patchCordL(mixer,            0, audioOut, 0);
AudioConnection patchCordR(mixer,            0, audioOut, 1);
#else
AudioOutputAnalog        audioOut;
AudioConnection patchCord0(playerMaterial0, 0, mixer,    0);
AudioConnection patchCord1(playerMaterial1, 0, mixer,    1);
AudioConnection patchCord2(playerTransfer,   0, mixer,    2);
AudioConnection patchCordOut(mixer,          0, audioOut, 0);
#endif

// ---------------------------------------------------------------------------
// Helpers — ported from proto.py
// ---------------------------------------------------------------------------

// [PORT] get_angular_distance()
float getAngularDistance(float a, float b) {
    float diff = fabs(fmodf(a - b, 360.0f));
    return min(diff, 360.0f - diff);
}

// [PORT] Look up the target angle for a given filename (item type keyword match).
float getAngleForType(const char* itemType) {
    for (int i = 0; i < ITEM_ANGLE_RULES_COUNT; i++) {
        if (strstr(itemType, ITEM_ANGLE_RULES[i].keyword) != nullptr) {
            return ITEM_ANGLE_RULES[i].angle;
        }
    }
    return ANGLE_FALLBACK;
}

// [PORT] pick_item_for_angle()
// Returns the filename of the selected asset, or nullptr if none found.
// `files` is a null-terminated array of filename strings on flash.
const char* pickItemForAngle(float inputAngle, float maxSpread,
                              const char** files, int fileCount) {
    // Two-pass: collect candidates with weights, then weighted-random pick.
    static float  weights[64];
    static int    indices[64];
    int           count = 0;
    float         totalWeight = 0.0f;

    for (int i = 0; i < fileCount && i < 64; i++) {
        float targetAngle = getAngleForType(files[i]);
        float dist = getAngularDistance(inputAngle, targetAngle);
        if (dist <= maxSpread) {
            float w = (maxSpread - dist);
            w = w * w;  // quadratic curve
            weights[count] = w;
            indices[count] = i;
            totalWeight += w;
            count++;
        }
    }

    if (count == 0 || totalWeight == 0.0f) {
        // Fallback: closest item
        float bestDist = 999.0f;
        int   bestIdx  = 0;
        for (int i = 0; i < fileCount; i++) {
            float d = getAngularDistance(inputAngle, getAngleForType(files[i]));
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        return files[bestIdx];
    }

    // Weighted random selection
    float r = (float)random(0, 10000) / 10000.0f * totalWeight;
    float cumulative = 0.0f;
    for (int i = 0; i < count; i++) {
        cumulative += weights[i];
        if (r <= cumulative) return files[indices[i]];
    }
    return files[indices[count - 1]];
}

// [PORT] Pick a random transfer sound matching the dynamic transfer layer keyword.
const char* pickTransferItem(const char** files, int fileCount) {
    static const char* transferCandidates[64];
    int count = 0;
    for (int i = 0; i < fileCount && i < 64; i++) {
        if (strstr(files[i], TRANSFER_LAYER_KEYWORD) != nullptr) {
            transferCandidates[count] = files[i];
            count++;
        }
    }
    if (count == 0) return nullptr;
    return transferCandidates[random(0, count)];
}

// ---------------------------------------------------------------------------
// Asset discovery — enumerate .raw files in the root of the SD card
// ---------------------------------------------------------------------------

static const char* sdFiles[64];
static int         sdFileCount = 0;

void discoverAssets() {
    sdFileCount = 0;
    File root = SD.open("/");
    static char nameBuf[64][32];
    while (sdFileCount < 64) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }
        const char* name = entry.name();
        const char* ext  = strrchr(name, '.');
        if (ext && strcasecmp(ext, ".raw") == 0) {
            strncpy(nameBuf[sdFileCount], name, 31);
            nameBuf[sdFileCount][31] = '\0';
            sdFiles[sdFileCount] = nameBuf[sdFileCount];
            sdFileCount++;
        }
        entry.close();
    }
    root.close();
    Serial.printf("Discovered %d audio files on SD card.\n", sdFileCount);
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

Bounce triggerBtn;
unsigned long lastPlayTime = 0;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    Serial.println("Lootr booting...");

    AudioMemory(12);

    // Trigger button (active LOW)
    pinMode(PIN_TRIGGER, INPUT_PULLUP);
    triggerBtn.attach(PIN_TRIGGER);
    triggerBtn.interval(10);

    // Joystick
    pinMode(PIN_JOYSTICK_X, INPUT);
    pinMode(PIN_JOYSTICK_Y, INPUT);

    // SD card
    if (!SD.begin(PIN_SD_CS)) {
        Serial.println("ERROR: SD card not found. Check wiring/card.");
        while (1);
    }

    discoverAssets();

    if (sdFileCount == 0) {
        Serial.println("WARNING: No .raw files found on SD card. Copy assets first.");
    }

    // Configure mixer channel gains for proper layering balance
    mixer.gain(0, 0.95f);  // material player 0
    mixer.gain(1, 0.95f);  // material player 1 (allows notes to overlap beautifully)
    mixer.gain(2, 0.45f);  // transfer layer player (quiet textural backing glue)

    Serial.println("Ready.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
    triggerBtn.update();
    bool triggerActive = (triggerBtn.read() == LOW);

    // Read joystick and normalise to -1.0 .. 1.0
    float x = (analogRead(PIN_JOYSTICK_X) - ADC_MIDPOINT) / (float)ADC_MIDPOINT;
    float y = (analogRead(PIN_JOYSTICK_Y) - ADC_MIDPOINT) / (float)ADC_MIDPOINT;

    float amplitude = sqrtf(x * x + y * y);
    float angleRad  = atan2f(y, x);
    float angleDeg  = fmodf(degrees(angleRad) + 360.0f, 360.0f);

    // [PORT] Dynamic spread — matches proto.py logic
    float t      = min(1.0f, amplitude);
    float spread = SPREAD_AT_CENTER - t * (SPREAD_AT_CENTER - SPREAD_AT_EDGE);

    unsigned long now = millis();

    if (amplitude > DEADZONE && triggerActive) {
        if ((now - lastPlayTime) >= PLAY_INTERVAL_MS && sdFileCount > 0) {
            const char* chosen = pickItemForAngle(angleDeg, spread, sdFiles, sdFileCount);
            if (chosen) {
                // Alternately play material sound on material0 / material1 to allow overlap!
                static bool alternate = false;
                if (alternate) {
                    playerMaterial0.play(chosen);
                } else {
                    playerMaterial1.play(chosen);
                }
                alternate = !alternate;

                // Play the sneaky transfer layer sound on the third player
                const char* transferChosen = pickTransferItem(sdFiles, sdFileCount);
                if (transferChosen && strcmp(chosen, transferChosen) != 0) {
                    playerTransfer.play(transferChosen);
                }

                Serial.printf("Angle: %5.1f | Amp: %.2f | Spread: %4.1f | File: %s\n",
                              angleDeg, amplitude, spread, chosen);
            }
            lastPlayTime = now;
        }
    }
}
