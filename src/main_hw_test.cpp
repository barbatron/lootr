// Lootr — Hardware Test Sketch
//
// Uses LittleFS (internal Teensy flash) instead of an SD card.
// Two build modes controlled by the PlatformIO environment:
//
//   teensy40_hwtest          — LittleFS + MTP (USB drive in Finder)
//                              Use this FIRST to load .raw files onto the Teensy.
//
//   teensy40_hwtest_usbaudio — LittleFS + USB Audio output
//                              After loading files, flash this and select
//                              "Teensy USB Audio" in macOS Sound settings.
//
// Files survive reflashing — LittleFS is non-volatile.
//
// Workflow:
//   1. ./scripts/prepare_assets_hw_test.sh
//   2. pio run -e teensy40_hwtest --target upload
//   3. Drag assets_test_raw/*.raw onto "Lootr Test" USB drive, eject
//   4. pio run -e teensy40_hwtest_usbaudio --target upload
//   5. System Preferences → Sound → Output → "Teensy USB Audio"
//   6. Use joystick
//
// NOTE: If you generated assets at a non-default sample rate, set
//   AUDIO_SAMPLE_RATE_EXACT below to match before flashing.

#include <Arduino.h>
#include <Audio.h>
#include <LittleFS.h>
#include <Bounce2.h>
#include <math.h>
#include <string.h>

#ifndef USB_AUDIO_OUT
#include <MTP_Teensy.h>
#endif

#include "config.h"

// Uncomment and set if you used --rate XXXX when preparing assets:
// #define AUDIO_SAMPLE_RATE_EXACT 22050.0f

// ---------------------------------------------------------------------------
// Audio objects
// AudioPlayQueue streams PCM chunks from LittleFS files each loop iteration.
// Output: USB audio (usbaudio env) or analog DAC (mtp env — nothing connected).
// ---------------------------------------------------------------------------

AudioPlayQueue    playerMaterial0;
AudioPlayQueue    playerMaterial1;
AudioPlayQueue    playerTransfer;
AudioMixer4       mixer;

#ifdef USB_AUDIO_OUT
// Stereo USB audio — Mac sees Teensy as a sound output device.
AudioOutputUSB    audioOut;
AudioConnection patchCord0(playerMaterial0, 0, mixer,    0);
AudioConnection patchCord1(playerMaterial1, 0, mixer,    1);
AudioConnection patchCord2(playerTransfer,   0, mixer,    2);
AudioConnection patchCordL(mixer,            0, audioOut, 0);  // left
AudioConnection patchCordR(mixer,            0, audioOut, 1);  // right
#else
// Analog DAC output — used by MTP build (nothing connected, but compiles fine).
AudioOutputAnalog audioOut;
AudioConnection patchCord0(playerMaterial0, 0, mixer,    0);
AudioConnection patchCord1(playerMaterial1, 0, mixer,    1);
AudioConnection patchCord2(playerTransfer,   0, mixer,    2);
AudioConnection patchCordOut(mixer,          0, audioOut, 0);
#endif

// ---------------------------------------------------------------------------
// LittleFS — internal flash partition
// ---------------------------------------------------------------------------

LittleFS_Program fs;
#define LOOTR_FS_SIZE (1024 * 1024)  // 1 MB; reduce if sketch grows too large

// ---------------------------------------------------------------------------
// Audio streaming state
// Each active file is read in 128-sample (256-byte) chunks and pushed into
// its AudioPlayQueue. Call updateAudioFeeders() every loop iteration.
// ---------------------------------------------------------------------------

struct PlayerState {
    AudioPlayQueue& queue;
    File            file;
    bool            active = false;
};

static PlayerState matPlayers[2] = { {playerMaterial0}, {playerMaterial1} };
static PlayerState tfPlayer      = { playerTransfer };

void startPlay(PlayerState& p, const char* filename) {
    if (p.file) p.file.close();
    p.file   = fs.open(filename, FILE_READ);
    p.active = (bool)p.file;
    if (!p.active) {
        Serial.printf("ERROR: cannot open '%s'\n", filename);
    }
}

static void feedPlayer(PlayerState& p) {
    if (!p.active) return;
    // Fill every available queue slot to keep the pipeline topped up.
    while (p.queue.available() > 0) {
        int16_t* buf = p.queue.getBuffer();
        if (!buf) break;
        int n = p.file.read((uint8_t*)buf, 128 * sizeof(int16_t));
        if (n <= 0) {
            // File exhausted — silence-pad this last buffer and stop.
            memset(buf, 0, 128 * sizeof(int16_t));
            p.queue.playBuffer();
            p.file.close();
            p.active = false;
            return;
        }
        if (n < (int)(128 * sizeof(int16_t))) {
            memset((uint8_t*)buf + n, 0, 128 * sizeof(int16_t) - n);
        }
        p.queue.playBuffer();
    }
}

void updateAudioFeeders() {
    feedPlayer(matPlayers[0]);
    feedPlayer(matPlayers[1]);
    feedPlayer(tfPlayer);
}

// ---------------------------------------------------------------------------
// Asset discovery — enumerate .raw files in the LittleFS root
// ---------------------------------------------------------------------------

static const char* fsFiles[64];
static int         fsFileCount = 0;

void discoverAssets() {
    fsFileCount = 0;
    File root = fs.open("/");
    static char nameBuf[64][32];
    while (fsFileCount < 64) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (entry.isDirectory()) { entry.close(); continue; }
        const char* name = entry.name();
        const char* ext  = strrchr(name, '.');
        if (ext && strcasecmp(ext, ".raw") == 0) {
            strncpy(nameBuf[fsFileCount], name, 31);
            nameBuf[fsFileCount][31] = '\0';
            fsFiles[fsFileCount]     = nameBuf[fsFileCount];
            fsFileCount++;
        }
        entry.close();
    }
    root.close();
    Serial.printf("Discovered %d .raw files on LittleFS.\n", fsFileCount);
    for (int i = 0; i < fsFileCount; i++) {
        Serial.printf("  [%d] %s\n", i, fsFiles[i]);
    }
}

// ---------------------------------------------------------------------------
// Algorithm — ported from proto.py (identical logic to main.cpp)
// ---------------------------------------------------------------------------

float getAngularDistance(float a, float b) {
    float diff = fabs(fmodf(a - b, 360.0f));
    return min(diff, 360.0f - diff);
}

float getAngleForType(const char* name) {
    for (int i = 0; i < ITEM_ANGLE_RULES_COUNT; i++) {
        if (strstr(name, ITEM_ANGLE_RULES[i].keyword) != nullptr) {
            return ITEM_ANGLE_RULES[i].angle;
        }
    }
    return ANGLE_FALLBACK;
}

const char* pickItemForAngle(float inputAngle, float maxSpread,
                              const char** files, int fileCount) {
    static float weights[64];
    static int   indices[64];
    int          count       = 0;
    float        totalWeight = 0.0f;

    for (int i = 0; i < fileCount && i < 64; i++) {
        float dist = getAngularDistance(inputAngle, getAngleForType(files[i]));
        if (dist <= maxSpread) {
            float w = (maxSpread - dist) * (maxSpread - dist);
            weights[count]  = w;
            indices[count]  = i;
            totalWeight    += w;
            count++;
        }
    }

    if (count == 0 || totalWeight == 0.0f) {
        float bestDist = 999.0f;
        int   bestIdx  = 0;
        for (int i = 0; i < fileCount; i++) {
            float d = getAngularDistance(inputAngle, getAngleForType(files[i]));
            if (d < bestDist) { bestDist = d; bestIdx = i; }
        }
        return files[bestIdx];
    }

    float r = (float)random(0, 10000) / 10000.0f * totalWeight;
    float cumulative = 0.0f;
    for (int i = 0; i < count; i++) {
        cumulative += weights[i];
        if (r <= cumulative) return files[indices[i]];
    }
    return files[indices[count - 1]];
}

const char* pickTransferItem(const char** files, int fileCount) {
    static const char* candidates[64];
    int count = 0;
    for (int i = 0; i < fileCount && i < 64; i++) {
        if (strstr(files[i], TRANSFER_LAYER_KEYWORD) != nullptr) {
            candidates[count++] = files[i];
        }
    }
    return (count > 0) ? candidates[random(0, count)] : nullptr;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

Bounce        triggerBtn;
unsigned long lastPlayTime  = 0;
bool          matAlternate  = false;

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("Lootr HW Test booting...");

    AudioMemory(20);

    // Mixer gains
    mixer.gain(0, 0.95f);
    mixer.gain(1, 0.95f);
    mixer.gain(2, 0.45f);

    // Trigger button
    pinMode(PIN_TRIGGER, INPUT_PULLUP);
    triggerBtn.attach(PIN_TRIGGER);
    triggerBtn.interval(10);

    // Joystick
    pinMode(PIN_JOYSTICK_X, INPUT);
    pinMode(PIN_JOYSTICK_Y, INPUT);

    // LittleFS
    if (!fs.begin(LOOTR_FS_SIZE)) {
        Serial.println("ERROR: LittleFS_Program could not be initialised.");
        while (1);
    }
    Serial.printf("LittleFS OK — %llu KB used of %llu KB\n",
                  fs.usedSize() / 1024, fs.totalSize() / 1024);

#ifdef USB_AUDIO_OUT
    // USB Audio mode: files already on LittleFS from a previous MTP flash.
    Serial.println("USB Audio mode.");
    Serial.println("Select 'Teensy USB Audio' in macOS System Settings > Sound > Output.");
#else
    // MTP mode: expose LittleFS as a USB drive for file transfer.
    MTP.begin();
    MTP.addFilesystem(fs, "Lootr Test");
    Serial.println("MTP active. Drag *.raw files onto 'Lootr Test' in Finder.");
    Serial.println("Then flash teensy40_hwtest_usbaudio to play.");
#endif
    Serial.println("");

    discoverAssets();

    if (fsFileCount == 0) {
        Serial.println("No .raw files found. Transfer assets via MTP, then reset.");
    }

    Serial.println("Ready. Hold joystick to play.");
}

// ---------------------------------------------------------------------------
// Loop
// ---------------------------------------------------------------------------

void loop() {
#ifndef USB_AUDIO_OUT
    // Keep MTP responsive while the USB drive is mounted
    MTP.loop();
#endif

    // Keep the audio queues topped up with data from open files
    updateAudioFeeders();

    triggerBtn.update();
    bool triggerActive = (triggerBtn.read() == LOW);

    float x = (analogRead(PIN_JOYSTICK_X) - ADC_MIDPOINT) / (float)ADC_MIDPOINT;
    float y = (analogRead(PIN_JOYSTICK_Y) - ADC_MIDPOINT) / (float)ADC_MIDPOINT;

    float amplitude = sqrtf(x * x + y * y);
    float angleRad  = atan2f(y, x);
    float angleDeg  = fmodf(degrees(angleRad) + 360.0f, 360.0f);

    float t      = min(1.0f, amplitude);
    float spread = SPREAD_AT_CENTER - t * (SPREAD_AT_CENTER - SPREAD_AT_EDGE);

    unsigned long now = millis();

    if (amplitude > DEADZONE && triggerActive && fsFileCount > 0) {
        if ((now - lastPlayTime) >= PLAY_INTERVAL_MS) {
            const char* chosen = pickItemForAngle(angleDeg, spread, fsFiles, fsFileCount);
            if (chosen) {
                startPlay(matPlayers[matAlternate ? 0 : 1], chosen);
                matAlternate = !matAlternate;

                const char* transfer = pickTransferItem(fsFiles, fsFileCount);
                if (transfer && strcmp(chosen, transfer) != 0) {
                    startPlay(tfPlayer, transfer);
                }

                Serial.printf("Angle: %5.1f° | Amp: %.2f | Spread: %4.1f° | %s\n",
                              angleDeg, amplitude, spread, chosen);
            }
            lastPlayTime = now;
        }
    }
}
