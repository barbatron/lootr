// Lootr — Loot-o-mo-tron
// Teensy 4.0 main sketch
//
// Ported from proto.py. See spec.md for full porting guide and wiring.
//
// Audio pipeline:
//   SerialFlash (SPI) → AudioPlaySerialflashRaw → AudioMixer4 → AudioOutputAnalog

#include <Arduino.h>
#include <Audio.h>
#include <SerialFlash.h>
#include <Bounce2.h>
#include <math.h>
#include <string.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Audio objects (Teensy Audio Library)
// ---------------------------------------------------------------------------

AudioPlaySerialflashRaw  player0;
AudioPlaySerialflashRaw  player1;
AudioMixer4              mixer;
AudioOutputAnalog        dac;

AudioConnection patchCord0(player0, 0, mixer, 0);
AudioConnection patchCord1(player1, 0, mixer, 1);
AudioConnection patchCordOut(mixer, 0, dac, 0);

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

// ---------------------------------------------------------------------------
// Asset discovery — enumerate files on SPI flash at startup
// ---------------------------------------------------------------------------

static const char* flashFiles[64];
static int         flashFileCount = 0;

void discoverAssets() {
    flashFileCount = 0;
    SerialFlash.opendir();
    static char nameBuf[64][32];
    while (flashFileCount < 64) {
        uint32_t size;
        if (!SerialFlash.readdir(nameBuf[flashFileCount], 32, size)) break;
        // Only include .raw files
        const char* ext = strrchr(nameBuf[flashFileCount], '.');
        if (ext && strcmp(ext, ".raw") == 0) {
            flashFiles[flashFileCount] = nameBuf[flashFileCount];
            flashFileCount++;
        }
    }
    Serial.printf("Discovered %d audio files on flash.\n", flashFileCount);
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

    // SPI Flash
    if (!SerialFlash.begin(PIN_FLASH_CS)) {
        Serial.println("ERROR: SPI flash not found. Check wiring.");
        while (1);
    }

    discoverAssets();

    if (flashFileCount == 0) {
        Serial.println("WARNING: No .raw files found on flash. Upload assets first.");
    }

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
        if ((now - lastPlayTime) >= PLAY_INTERVAL_MS && flashFileCount > 0) {
            const char* chosen = pickItemForAngle(angleDeg, spread, flashFiles, flashFileCount);
            if (chosen) {
                // Play on whichever player is free
                if (!player0.isPlaying()) {
                    player0.play(chosen);
                } else if (!player1.isPlaying()) {
                    player1.play(chosen);
                }
                Serial.printf("Angle: %5.1f | Amp: %.2f | Spread: %4.1f | File: %s\n",
                              angleDeg, amplitude, spread, chosen);
            }
            lastPlayTime = now;
        }
    }
}
