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
    print("pip install pygame")
    sys.exit(1)

ASSETS_DIR = "assets"
PLAY_INTERVAL_MS = 200

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

def map_types_to_angles(item_types):
    """
    Assigns an angle (in degrees) to each item type.
    You can manually configure these later.
    """
    configured_angles = {}
    types_list = list(item_types.keys())
    types_list.sort()
    
    if not types_list:
        return {}
        
    for item_type in types_list:
        lower_type = item_type.lower()
        if "metal" in lower_type or "can" in lower_type or "gun" in lower_type:
            configured_angles[item_type] = 270.0  # Straight up
        elif "charcoal" in lower_type or "sulfur" in lower_type or "stone" in lower_type or "ore" in lower_type:
            configured_angles[item_type] = 180.0  # Left
        elif "wood" in lower_type:
            configured_angles[item_type] = 0.0    # Right
        else:
            configured_angles[item_type] = 90.0   # Down
        
    return configured_angles

def get_angular_distance(angle1, angle2):
    diff = abs((angle1 - angle2) % 360)
    return min(diff, 360 - diff)

def pick_item_for_angle(input_angle, configured_angles, max_spread=45.0):
    """
    Pick an item type based on the input angle.
    Items closer to the angle have a higher probability.
    max_spread defines how far (in degrees) to allow randomness.
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
        
        # Deadzone handling to avoid random noise when stick is centered
        if amplitude > 0.2 and trigger_active:
            if current_time - last_play_time >= PLAY_INTERVAL_MS:
                # Pick item type
                picked_type = pick_item_for_angle(angle_deg, angles_map)
                
                if picked_type:
                    # Pick random variation
                    sound_list = sounds[picked_type]
                    sound_to_play = random.choice(sound_list)
                    
                    # Play the sound
                    # We can use amplitude to control the volume!
                    volume = min(1.0, max(0.0, amplitude))
                    sound_to_play.set_volume(volume)
                    sound_to_play.play()
                    
                    print(f"Angle: {angle_deg:5.1f}° | Amp: {amplitude:4.2f} | Picked: {picked_type:20s}")
                
                last_play_time = current_time
                
        # Small sleep to yield CPU
        time.sleep(0.01)
        
    pygame.quit()

if __name__ == "__main__":
    main()
