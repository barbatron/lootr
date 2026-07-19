"""
proto.py — Lootr Loot-o-mo-tron PC Prototype
=============================================
This file is the canonical source of truth for the Lootr audio selection logic.
It is designed to be runnable on PC (via Pygame) for rapid iteration, and to be
portable to C++ for the Teensy 4.0 hardware build.

Hardware Target (for porting reference — see spec.md):
  - MCU:      Teensy 4.0
  - Input:    KY-023 analog thumbstick (X→A0, Y→A1, SW→D2)
  - Storage:  SPI flash chip, e.g. W25Q128 (CS→D10, MOSI→D11, MISO→D12, SCK→D13)
  - Audio:    Teensy DAC (pin 14/A14) → LM386 amp → 8Ω speaker
  - Power:    LiPo + 5V boost converter

Porting notes:
  - All [PORT] functions/constants have direct C++ equivalents described in spec.md
  - map_types_to_angles() → hardcoded lookup in config.h on hardware
  - pick_item_for_angle() → port verbatim, uses only math and arrays
  - get_angular_distance() → port verbatim, pure math
  - PLAY_INTERVAL_MS, DEADZONE, SPREAD_* constants → config.h
"""

import os
import glob
import math
import random
import time
import re
import sys

# Try importing pygame for PC prototyping
try:
    import pygame
except ImportError:
    print("Pygame is required for the PC prototype. Please install it using:")
    print("uv pip install pygame  (or: uv add pygame)")
    sys.exit(1)

# ---------------------------------------------------------------------------
# [PORT] CONFIGURATION — mirrors config.h on hardware
# ---------------------------------------------------------------------------

ASSETS_DIR = "assets"

# Minimum time between sample triggers (milliseconds)
# Configured to 120ms to match Rust's rapid and satisfying hover-loot pacing
PLAY_INTERVAL_MS = 175

# Generic "transfer" layer sound context — plays simultaneously with the material sound
# to glue the rapid-fire triggers together into a physical "mass loot" feeling.
TRANSFER_LAYER_TYPE = "cloth"

# Joystick deadzone: below this amplitude, no sample is triggered.
# Prevents noise when stick is centered.
DEADZONE = 0.05

# Spread at minimum amplitude (stick barely pushed) → fully random selection
SPREAD_AT_CENTER = 180.0

# Spread at maximum amplitude (stick fully pushed) → tight, directional selection
SPREAD_AT_EDGE = 20.0

def load_assets():
    """
    Scans the assets directory and groups variations by item type.
    """
    item_types = {}
    
    # We expect files like ui-pickup-{type}-{variation}.wav
    # or {type}-{variation}.wav
    pattern = os.path.join(ASSETS_DIR, "*.wav")
    files = glob.glob(pattern)
    
    for filepath in files:
        filename = os.path.basename(filepath)
        name_no_ext = os.path.splitext(filename)[0]
        
        # Remove 'ui-pickup-' prefix if it exists
        if name_no_ext.startswith("ui-pickup-"):
            name_no_ext = name_no_ext[len("ui-pickup-"):]
            
        # Try to find a trailing number denoting the variation (e.g., -1, -001)
        match = re.match(r"(.*)-(\d+)$", name_no_ext)
        if match:
            item_type = match.group(1)
        else:
            item_type = name_no_ext
            
        if item_type not in item_types:
            item_types[item_type] = []
        item_types[item_type].append(filepath)
        
    return item_types

# [PORT] Item-type → angle mapping.
# On hardware, replace this function with a static lookup table in config.h.
# Angle convention: 0° = right, 90° = down, 180° = left, 270° = up.
ITEM_ANGLE_RULES = [
    # (keywords,                                              angle_deg)
    (["metal", "can", "gun", "pipe", "blade", "wire"],        270.0),  # Up    — metal/mechanical
    (["charcoal", "sulfur", "sulphur", "stone", "ore", "coal"], 180.0),  # Left  — minerals/earth
    (["wood", "plank", "stick", "log"],                        0.0),    # Right — wood
    # Fallback → Down (90°)
]

def map_types_to_angles(item_types):
    """
    [PORT] Assigns a target angle (degrees) to each discovered item type.

    Rules are defined in ITEM_ANGLE_RULES above. Anything not matched falls
    back to 90° (down). On hardware, this becomes a static table in config.h
    since the asset list is known at build time.
    """
    configured_angles = {}
    types_list = list(item_types.keys())
    types_list.sort()
    
    if not types_list:
        return {}
        
    for item_type in types_list:
        lower_type = item_type.lower()
        angle = 90.0  # Default: down
        for keywords, rule_angle in ITEM_ANGLE_RULES:
            if any(kw in lower_type for kw in keywords):
                angle = rule_angle
                break
        configured_angles[item_type] = angle
        
    return configured_angles

# [PORT] Pure math — port verbatim to C++.
def get_angular_distance(angle1, angle2):
    """Returns the shortest angular distance (0–180°) between two angles."""
    diff = abs((angle1 - angle2) % 360)
    return min(diff, 360 - diff)

# [PORT] Core selection algorithm — port verbatim to C++.
def pick_item_for_angle(input_angle, configured_angles, max_spread=45.0):
    """
    [PORT] Selects an item type probabilistically based on joystick angle.

    Only item types within max_spread degrees of input_angle are candidates.
    Closer items are weighted quadratically. Falls back to the nearest item
    if nothing is within spread.

    Args:
        input_angle:       Current joystick angle in degrees (0=right, 90=down).
        configured_angles: Dict of {item_type: target_angle}.
        max_spread:        Maximum angular window for candidate selection (degrees).
                           Driven dynamically by amplitude — see SPREAD_AT_CENTER/EDGE.
    """
    if not configured_angles:
        return None
        
    weights = []
    types = []
    
    for item_type, target_angle in configured_angles.items():
        dist = get_angular_distance(input_angle, target_angle)
        
        if dist <= max_spread:
            weight = max(0, max_spread - dist)
            weight = weight ** 2  # curve it so closer items are favored significantly more
            
            types.append(item_type)
            weights.append(weight)
            
    if not types or sum(weights) == 0:
        # Fallback to the closest item if nothing is strictly within the spread
        return min(configured_angles.keys(), key=lambda k: get_angular_distance(input_angle, configured_angles[k]))
        
    # Random choice weighted by distance
    return random.choices(types, weights=weights, k=1)[0]


def main():
    pygame.init()
    pygame.mixer.init()
    
    # Setup Joystick
    pygame.joystick.init()
    joystick = None
    if pygame.joystick.get_count() > 0:
        joystick = pygame.joystick.Joystick(0)
        joystick.init()
        print(f"Detected Joystick: {joystick.get_name()}")
    else:
        print("No joystick detected. You can use the mouse for X/Y input on the window!")

    # Set up a dummy window to capture events
    screen = pygame.display.set_mode((400, 400))
    pygame.display.set_caption("Lootr Audio Tester")
    
    print("Loading assets...")
    item_variations = load_assets()
    if not item_variations:
        print(f"No WAV files found in '{ASSETS_DIR}'")
        return
        
    # Preload sounds into pygame mixer
    print("Preloading sounds into memory...")
    sounds = {}
    for item_type, filepaths in item_variations.items():
        sounds[item_type] = []
        for path in filepaths:
            sounds[item_type].append(pygame.mixer.Sound(path))
            
    angles_map = map_types_to_angles(item_variations)
    print("\n--- Current Angle Configuration ---")
    for item_type, angle in angles_map.items():
        print(f"{item_type}: {angle:.1f} degrees")
    print("-----------------------------------")
    
    print("\nReady! Use your joystick (X/Y axis and Button 0) or move mouse + click.")

    last_play_time = 0
    running = True

    # State variables
    input_x = 0.0
    input_y = 0.0
    trigger_active = False

    while running:
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                running = False
                
            # If no joystick, fallback to mouse
            if joystick is None:
                if event.type == pygame.MOUSEMOTION:
                    # Map screen (0..400) to (-1.0 to 1.0)
                    input_x = (event.pos[0] - 200) / 200.0
                    input_y = (event.pos[1] - 200) / 200.0
                elif event.type == pygame.MOUSEBUTTONDOWN:
                    trigger_active = True
                elif event.type == pygame.MOUSEBUTTONUP:
                    trigger_active = False

        if joystick:
            input_x = joystick.get_axis(0)
            input_y = joystick.get_axis(1) # Note: Y is usually inverted on gamepads
            trigger_active = joystick.get_button(0) # Change index if using a different trigger
            
        current_time = pygame.time.get_ticks()
        
        # Calculate angle and amplitude
        amplitude = math.hypot(input_x, input_y)
        # Note: math.atan2(y, x) returns radians from -pi to pi.
        # We convert to degrees. Let's make 0 degrees = right, 90 = down etc.
        angle_rad = math.atan2(input_y, input_x)
        angle_deg = math.degrees(angle_rad) % 360
        
        # [PORT] Deadzone + dynamic spread — see DEADZONE, SPREAD_AT_CENTER/EDGE in config.
        if amplitude > DEADZONE and trigger_active:
            if current_time - last_play_time >= PLAY_INTERVAL_MS:
                # Dynamic spread: center stick → fully random, full push → tight selection.
                dynamic_spread = SPREAD_AT_CENTER - (min(1.0, amplitude) * (SPREAD_AT_CENTER - SPREAD_AT_EDGE))
                
                # Pick item type
                picked_type = pick_item_for_angle(angle_deg, angles_map, max_spread=dynamic_spread)
                
                if picked_type:
                    # Pick random variation
                    sound_list = sounds[picked_type]
                    sound_to_play = random.choice(sound_list)
                    
                    # Play the sound with subtle organic volume variation to reduce repetitive feel
                    material_volume = random.uniform(0.9, 1.0)
                    sound_to_play.set_volume(material_volume)
                    sound_to_play.play()
                    
                    # Sneaky "transfer" layering for that iconic Rust hover-loot feeling!
                    # Play a short generic texturing sound containing cloth/paper underneath the material
                    if TRANSFER_LAYER_TYPE in sounds and TRANSFER_LAYER_TYPE != picked_type:
                        transfer_sound = random.choice(sounds[TRANSFER_LAYER_TYPE])
                        transfer_volume = random.uniform(0.4, 0.6)
                        transfer_sound.set_volume(transfer_volume)
                        transfer_sound.play()
                    
                    print(f"Angle: {angle_deg:5.1f}° | Amp: {amplitude:4.2f} | Picked: {picked_type:20s}")
                
                last_play_time = current_time
                
        # Small sleep to yield CPU
        time.sleep(0.01)
        
    pygame.quit()

if __name__ == "__main__":
    main()
