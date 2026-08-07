// Lootr — SD diagnostics firmware
//
// Purpose:
// 1) Verify SPI wiring and SD init on Teensy 4.0
// 2) Detect intermittent connection failures from flimsy wires
// 3) Confirm .raw file visibility from card root

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "config.h"

static const int CS_CANDIDATES[] = {PIN_SD_CS, 4, 5, 9, 10, 15, 20};
static const int CS_CANDIDATE_COUNT = sizeof(CS_CANDIDATES) / sizeof(CS_CANDIDATES[0]);

int activeCs = -1;
unsigned long passCount = 0;
unsigned long failCount = 0;

void printWiringGuide() {
    Serial.println("Expected wiring (Teensy 4.0):");
    Serial.println("  SD CS   -> pin 10");
    Serial.println("  SD SCK  -> pin 13");
    Serial.println("  SD MOSI -> pin 11");
    Serial.println("  SD MISO -> pin 12");
    Serial.println("  SD VCC  -> 3.3V");
    Serial.println("  SD GND  -> GND");
    Serial.println();
}

bool countRawFiles(int* outRawCount, int* outAllCount) {
    File root = SD.open("/");
    if (!root) return false;

    int rawCount = 0;
    int allCount = 0;

    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            allCount++;
            const char* name = entry.name();
            const char* ext = strrchr(name, '.');
            if (ext && strcasecmp(ext, ".raw") == 0) rawCount++;
        }
        entry.close();
    }
    root.close();

    *outRawCount = rawCount;
    *outAllCount = allCount;
    return true;
}

void printPinLevels() {
    pinMode(11, INPUT_PULLUP);
    pinMode(12, INPUT_PULLUP);
    pinMode(13, INPUT_PULLUP);
    pinMode(PIN_SD_CS, INPUT_PULLUP);

    Serial.printf("Pin levels (idle): CS(%d)=%d MOSI(11)=%d MISO(12)=%d SCK(13)=%d\n",
                  PIN_SD_CS,
                  digitalRead(PIN_SD_CS),
                  digitalRead(11),
                  digitalRead(12),
                  digitalRead(13));
}

bool tryInitWithCs(int csPin) {
    pinMode(csPin, OUTPUT);
    digitalWrite(csPin, HIGH);
    delay(5);

    if (!SD.begin(csPin)) {
        Serial.printf("  CS=%d -> SD.begin FAILED\n", csPin);
        return false;
    }

    int rawCount = 0;
    int allCount = 0;
    if (!countRawFiles(&rawCount, &allCount)) {
        Serial.printf("  CS=%d -> SD.begin OK, root open FAILED\n", csPin);
        return false;
    }

    Serial.printf("  CS=%d -> SD.begin OK, files=%d, raw=%d\n", csPin, allCount, rawCount);
    activeCs = csPin;
    return true;
}

bool scanCsPins() {
    Serial.println("Scanning CS candidates...");
    activeCs = -1;
    bool found = false;

    for (int i = 0; i < CS_CANDIDATE_COUNT; i++) {
        int cs = CS_CANDIDATES[i];
        if (tryInitWithCs(cs)) {
            found = true;
            break;
        }
    }

    if (!found) {
        Serial.println("No working CS pin found.");
    }
    return found;
}

void setup() {
    Serial.begin(115200);
    delay(400);

    Serial.println();
    Serial.println("=== Lootr SD Diagnostics ===");
    printWiringGuide();
    printPinLevels();
    SPI.begin();

    if (!scanCsPins()) {
        Serial.println("Hint: check VCC/GND first, then CS/MISO/MOSI/SCK order.");
    }

    Serial.println("Commands: r=rescan  p=pin-levels  s=status");
    Serial.println();
}

void runHealthCheck() {
    if (activeCs < 0) {
        failCount++;
        Serial.printf("[FAIL %lu] no active CS\n", failCount);
        return;
    }

    if (!SD.begin(activeCs)) {
        failCount++;
        Serial.printf("[FAIL %lu] SD.begin failed on CS=%d\n", failCount, activeCs);
        return;
    }

    int rawCount = 0;
    int allCount = 0;
    if (!countRawFiles(&rawCount, &allCount)) {
        failCount++;
        Serial.printf("[FAIL %lu] root open/list failed on CS=%d\n", failCount, activeCs);
        return;
    }

    passCount++;
    Serial.printf("[PASS %lu] CS=%d files=%d raw=%d\n", passCount, activeCs, allCount, rawCount);
}

void loop() {
    static unsigned long last = 0;
    unsigned long now = millis();

    if (Serial.available()) {
        char c = (char)Serial.read();
        if (c == 'r' || c == 'R') {
            scanCsPins();
        } else if (c == 'p' || c == 'P') {
            printPinLevels();
        } else if (c == 's' || c == 'S') {
            Serial.printf("status: activeCs=%d pass=%lu fail=%lu\n", activeCs, passCount, failCount);
        }
    }

    // Stress check every 500 ms to catch flaky jumper contact.
    if (now - last >= 500) {
        runHealthCheck();
        last = now;
    }
}
