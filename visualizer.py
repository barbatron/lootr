import pygame
import math

class LootrVisualizer:
    def __init__(self, width=400, height=400):
        self.width = width
        self.height = height
        self.center_x = width // 2
        self.center_y = height // 2
        self.radius = min(self.center_x, self.center_y) - 40
        self.font = pygame.font.SysFont("Courier", 12, bold=True)
        self.large_font = pygame.font.SysFont("Courier", 14, bold=True)
        self.last_picked_item = ""
        self.last_played_time = 0

    def set_last_picked(self, item_name):
        self.last_picked_item = item_name
        self.last_played_time = pygame.time.get_ticks()

    def draw(self, screen, input_x, input_y, angle_deg, amplitude, dynamic_spread, trigger_active, angles_map):
        # Background: Dark Charcoal
        screen.fill((24, 24, 24))

        # 1. Draw outer boundary / joystick rim
        pygame.draw.circle(screen, (50, 50, 50), (self.center_x, self.center_y), self.radius, 2)
        pygame.draw.circle(screen, (40, 40, 40), (self.center_x, self.center_y), 5)  # Center deadzone tick

        # 2. Draw 4 main directions with sector labels and angles
        # Up (270), Left (180), Right (0), Down (90)
        directions = [
            ("WOOD (Right)", 0, (200, 150, 100)),
            ("FALLBACK (Down)", 90, (150, 150, 150)),
            ("ORE/STONE/SULFUR (Left)", 180, (220, 200, 140)),
            ("METAL/CAN/GUN (Up)", 270, (180, 220, 240)),
        ]

        for label, angle_rule, color in directions:
            angle_r = math.radians(angle_rule)
            end_x = self.center_x + int(math.cos(angle_r) * self.radius)
            end_y = self.center_y + int(math.sin(angle_r) * self.radius)
            pygame.draw.line(screen, (40, 40, 40), (self.center_x, self.center_y), (end_x, end_y), 1)

            # Draw label slightly outside the rim
            text_x = self.center_x + int(math.cos(angle_r) * (self.radius + 20))
            text_y = self.center_y + int(math.sin(angle_r) * (self.radius + 15))
            txt_surf = self.font.render(label, True, color)
            txt_rect = txt_surf.get_rect(center=(text_x, text_y))
            screen.blit(txt_surf, txt_rect)

        # 3. Draw direction guide of discovered item types
        # This lists specific registered targets along the rim
        drawn_labels = set()
        for item_type, target_angle in angles_map.items():
            if target_angle not in drawn_labels:
                drawn_labels.add(target_angle)
                angle_r = math.radians(target_angle)
                tick_x = self.center_x + int(math.cos(angle_r) * self.radius)
                tick_y = self.center_y + int(math.sin(angle_r) * self.radius)
                pygame.draw.circle(screen, (100, 100, 100), (tick_x, tick_y), 3)

        # 4. Draw Spread Wedge/Cone (Translucent sector)
        # We need to visualize the current spread angle spanning input_angle ± dynamic_spread
        if amplitude > 0.05:
            spread_color = (255, 120, 0, 40) if trigger_active else (100, 100, 100, 30)
            
            # Draw spread sweep area (drawn using polygon approximation)
            wedge_surf = pygame.Surface((self.width, self.height), pygame.SRCALPHA)
            points = [(self.center_x, self.center_y)]
            
            start_angle = angle_deg - dynamic_spread
            end_angle = angle_deg + dynamic_spread
            
            steps = 24
            for i in range(steps + 1):
                cur_deg = start_angle + (end_angle - start_angle) * (i / steps)
                cur_rad = math.radians(cur_deg)
                px = self.center_x + int(math.cos(cur_rad) * self.radius)
                py = self.center_y + int(math.sin(cur_rad) * self.radius)
                points.append((px, py))
                
            if len(points) >= 3:
                pygame.draw.polygon(wedge_surf, spread_color, points)
                screen.blit(wedge_surf, (0,0))
                
            # Draw exact angle line
            angle_r = math.radians(angle_deg)
            target_pos = (
                self.center_x + int(math.cos(angle_r) * self.radius),
                self.center_y + int(math.sin(angle_r) * self.radius)
            )
            line_color = (255, 102, 0) if trigger_active else (200, 200, 200)
            pygame.draw.line(screen, line_color, (self.center_x, self.center_y), target_pos, 2)

        # 5. Draw Joystick/Mouse reticle
        # Bound coordinates to visual screen space
        reticle_x = self.center_x + int(input_x * self.radius)
        reticle_y = self.center_y + int(input_y * self.radius)
        
        # Color corresponds to active / triggered state
        color_triggered = (220, 40, 40) if trigger_active else (40, 220, 40)
        pygame.draw.circle(screen, color_triggered, (reticle_x, reticle_y), 8, 2)
        pygame.draw.circle(screen, color_triggered, (reticle_x, reticle_y), 2)

        # 6. Draw Text Overlays
        y_offset = 10
        info_lines = [
            (f"X: {input_x:5.2f} | Y: {input_y:5.2f}", (180, 180, 180)),
            (f"Angle : {angle_deg:5.1f}°", (255, 255, 255)),
            (f"Amp   : {amplitude:5.2f}", (255, 255, 255)),
            (f"Spread: ±{dynamic_spread:4.1f}°", (100, 200, 255)),
            (f"Trigger: {'ACTIVE' if trigger_active else 'INACTIVE'}", color_triggered),
        ]

        for text, color in info_lines:
            txt_surf = self.font.render(text, True, color)
            screen.blit(txt_surf, (10, y_offset))
            y_offset += 16

        # Draw last picked item
        if self.last_picked_item:
            elapsed = pygame.time.get_ticks() - self.last_played_time
            # Flash text briefly when triggered, otherwise fade slightly
            text_color = (255, 255, 255) if elapsed < 100 else (200, 200, 200)
            picked_title = self.font.render("Last Looted:", True, (150, 150, 150))
            picked_val = self.large_font.render(self.last_picked_item, True, text_color)
            screen.blit(picked_title, (10, self.height - 35))
            screen.blit(picked_val, (10, self.height - 20))
            
            # Draw flash ring around center on trigger
            if elapsed < 80:
                pygame.draw.circle(screen, (255, 102, 0, 150), (self.center_x, self.center_y), self.radius, 4)
