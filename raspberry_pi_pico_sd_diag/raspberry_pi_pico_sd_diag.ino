#include <SPI.h>
#include <SD.h>
#include <ctype.h>

// Raspberry Pi Pico wiring for this sketch (SPI0):
// GP18 -> SCK, GP19 -> MOSI, GP16 -> MISO, GP17 -> CS
static const uint8_t PIN_SD_CS = 17;
static const uint8_t PIN_SD_SCK = 18;
static const uint8_t PIN_SD_MOSI = 19;
static const uint8_t PIN_SD_MISO = 16;

const uint8_t CS_CANDIDATES[] = {PIN_SD_CS, 5, 9, 10, 22};
const uint8_t CS_COUNT = sizeof(CS_CANDIDATES) / sizeof(CS_CANDIDATES[0]);

int8_t activeCs = -1;
unsigned long passCount = 0;
unsigned long failCount = 0;

bool hasRawExtension(const char* name) {
  size_t n = strlen(name);
  if (n < 4) return false;
  char c1 = tolower(name[n - 4]);
  char c2 = tolower(name[n - 3]);
  char c3 = tolower(name[n - 2]);
  char c4 = tolower(name[n - 1]);
  return (c1 == '.' && c2 == 'r' && c3 == 'a' && c4 == 'w');
}

bool countFiles(int* rawCount, int* allCount) {
  File root = SD.open("/");
  if (!root) return false;
  if (!root.isDirectory()) {
    root.close();
    return false;
  }

  int raw = 0;
  int all = 0;

  while (true) {
    File entry = root.openNextFile();
    if (!entry) break;

    if (!entry.isDirectory()) {
      all++;
      if (hasRawExtension(entry.name())) raw++;
    }
    entry.close();
  }

  root.close();
  *rawCount = raw;
  *allCount = all;
  return true;
}

bool tryInitWithCs(uint8_t csPin) {
  pinMode(csPin, OUTPUT);
  digitalWrite(csPin, HIGH);
  delay(5);

  if (!SD.begin(csPin)) {
    Serial.print("  CS=");
    Serial.print(csPin);
    Serial.println(" -> SD.begin FAILED");
    return false;
  }

  int rawCount = 0;
  int allCount = 0;
  if (!countFiles(&rawCount, &allCount)) {
    Serial.print("  CS=");
    Serial.print(csPin);
    Serial.println(" -> SD.begin OK, root read FAILED");
    return false;
  }

  Serial.print("  CS=");
  Serial.print(csPin);
  Serial.print(" -> SD.begin OK, files=");
  Serial.print(allCount);
  Serial.print(", raw=");
  Serial.println(rawCount);

  activeCs = (int8_t)csPin;
  return true;
}

bool scanCsPins() {
  activeCs = -1;
  Serial.println("Scanning CS candidates...");

  for (uint8_t i = 0; i < CS_COUNT; i++) {
    if (tryInitWithCs(CS_CANDIDATES[i])) return true;
  }

  Serial.println("No working CS pin found.");
  return false;
}

void printHelp() {
  Serial.println("Commands:");
  Serial.println("  r = rescan CS pins");
  Serial.println("  s = status");
}

void runHealthCheck() {
  if (activeCs < 0) {
    failCount++;
    Serial.print("[FAIL ");
    Serial.print(failCount);
    Serial.println("] no active CS");
    return;
  }

  if (!SD.begin((uint8_t)activeCs)) {
    failCount++;
    Serial.print("[FAIL ");
    Serial.print(failCount);
    Serial.print("] SD.begin failed on CS=");
    Serial.println(activeCs);
    return;
  }

  int rawCount = 0;
  int allCount = 0;
  if (!countFiles(&rawCount, &allCount)) {
    failCount++;
    Serial.print("[FAIL ");
    Serial.print(failCount);
    Serial.print("] root read failed on CS=");
    Serial.println(activeCs);
    return;
  }

  passCount++;
  Serial.print("[PASS ");
  Serial.print(passCount);
  Serial.print("] CS=");
  Serial.print(activeCs);
  Serial.print(" files=");
  Serial.print(allCount);
  Serial.print(" raw=");
  Serial.println(rawCount);
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Serial.println();
  Serial.println("=== Pico SD Diagnostics ===");
  Serial.println("Expected SPI0 wiring:");
  Serial.println("  CS   -> GP17");
  Serial.println("  SCK  -> GP18");
  Serial.println("  MOSI -> GP19");
  Serial.println("  MISO -> GP16");
  Serial.println();

  // Earle Philhower RP2040 core required — NOT the Arduino Mbed RP2040 core.
  // Install: https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
  SPI.setRX(PIN_SD_MISO);
  SPI.setTX(PIN_SD_MOSI);
  SPI.setSCK(PIN_SD_SCK);
  SPI.begin();

  scanCsPins();
  printHelp();
  Serial.println();
}

void loop() {
  static unsigned long last = 0;
  unsigned long now = millis();

  if (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'r' || c == 'R') {
      scanCsPins();
    } else if (c == 's' || c == 'S') {
      Serial.print("status: activeCs=");
      Serial.print(activeCs);
      Serial.print(" pass=");
      Serial.print(passCount);
      Serial.print(" fail=");
      Serial.println(failCount);
    }
  }

  if (now - last >= 1000) {
    runHealthCheck();
    last = now;
  }
}
