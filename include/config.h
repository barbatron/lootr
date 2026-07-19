#pragma once

// ---------------------------------------------------------------------------
// Lootr — config.h
// Keep these values in sync with the constants in proto.py
// ---------------------------------------------------------------------------

// --- Pin assignments -------------------------------------------------------

#define PIN_JOYSTICK_X   A0   // KY-023 VRx
#define PIN_JOYSTICK_Y   A1   // KY-023 VRy
#define PIN_TRIGGER      2    // KY-023 SW (active LOW, INPUT_PULLUP)

#define PIN_FLASH_CS     10   // SPI flash chip-select

// Audio out via Teensy DAC (A14) is used automatically by AudioOutputAnalog.
// If switching to I2S, set up MCLK=22, BCLK=21, LRCLK=20, DIN=7.

// --- Timing ---------------------------------------------------------------

#define PLAY_INTERVAL_MS  180   // Minimum ms between sample triggers

// --- Joystick -------------------------------------------------------------

#define DEADZONE          0.05f  // Amplitude below which no trigger fires
#define ADC_MIDPOINT      512    // Centre ADC reading (0–1023 range)

// --- Spread (item selection randomness) -----------------------------------
// At low amplitude (stick near centre) → wide spread → random selection.
// At full amplitude (stick fully pushed) → narrow spread → directional.

#define SPREAD_AT_CENTER  180.0f  // degrees — fully random
#define SPREAD_AT_EDGE     20.0f  // degrees — tight

// --- Item type → angle table (0°=right, 90°=down, 180°=left, 270°=up) ----
// Add/remove entries here. The porting guide in spec.md describes the rules.

struct ItemAngleRule {
    const char* keyword;
    float       angle;
};

// Checked in order; first match wins. Fallback is 90.0f (down).
static const ItemAngleRule ITEM_ANGLE_RULES[] = {
    { "metal",   270.0f },
    { "can",     270.0f },
    { "gun",     270.0f },
    { "pipe",    270.0f },
    { "blade",   270.0f },
    { "wire",    270.0f },
    { "charcoal",180.0f },
    { "sulfur",  180.0f },
    { "sulphur", 180.0f },
    { "stone",   180.0f },
    { "ore",     180.0f },
    { "coal",    180.0f },
    { "wood",      0.0f },
    { "plank",     0.0f },
    { "stick",     0.0f },
    { "log",       0.0f },
};

static const int ITEM_ANGLE_RULES_COUNT =
    sizeof(ITEM_ANGLE_RULES) / sizeof(ITEM_ANGLE_RULES[0]);

#define ANGLE_FALLBACK 90.0f  // Down — anything not matched above
