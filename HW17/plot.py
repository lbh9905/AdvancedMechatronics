import serial
import serial.tools.list_ports
import pygame
import sys
import math

def find_pico_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if 'USB' in p.description or 'Pico' in p.description:
            return p.device
    return None

PORT = find_pico_port()
if PORT is None:
    print("Could not find Pico!")
    sys.exit()

ser = serial.Serial(PORT, 115200, timeout=0)
print(f"Connected on {PORT}")

# ── auto-tare ────────────────────────────────────────────────────
print("Taring... don't touch the load cell")
tare_samples = []
tare_buf = ""
while len(tare_samples) < 20:
    try:
        raw = ser.read(ser.in_waiting or 1)
        tare_buf += raw.decode(errors='ignore')
        while '\n' in tare_buf:
            line, tare_buf = tare_buf.split('\n', 1)
            line = line.strip()
            if ',' in line:
                parts = line.split(',')
                if len(parts) == 2:
                    try:
                        tare_samples.append(float(parts[1]))
                    except:
                        pass
    except:
        pass
FORCE_ZERO = sum(tare_samples) / len(tare_samples)
print(f"Tare complete. Zero = {FORCE_ZERO:.0f}")

# ── constants ────────────────────────────────────────────────────
ANGLE_MIN     = 226.0
ANGLE_MAX     = 419.0
ANGLE_CENTER  = (ANGLE_MIN + ANGLE_MAX) / 2.0
FORCE_DEAD    = 15000.0
NEEDLE_SPEED  = 0.004
SETTLE_FRAMES = 40

WIDTH, HEIGHT = 900, 650
BAR_X    = 810
BAR_Y    = 80
BAR_W    = 36
BAR_H    = 480
STEM_X   = 420
STEM_Y   = HEIGHT - 60
STEM_LEN = 300   # bigger flower

pygame.init()
screen = pygame.display.set_mode((WIDTH, HEIGHT))
pygame.display.set_caption("Flower Controller")
clock = pygame.time.Clock()
font    = pygame.font.SysFont("Arial", 22, bold=True)
font_sm = pygame.font.SysFont("Arial", 18)
font_hint = pygame.font.SysFont("Arial", 16)

# ── rainbow bar ──────────────────────────────────────────────────
rainbow_surf = pygame.Surface((BAR_W, BAR_H))
for i in range(BAR_H):
    hue = (i / BAR_H) * 360
    color = pygame.Color(0)
    color.hsva = (hue, 100, 100, 100)
    pygame.draw.line(rainbow_surf, color, (0, i), (BAR_W, i))

def hue_to_rgb(hue):
    c = pygame.Color(0)
    c.hsva = (hue % 360, 90, 95, 100)
    return (c.r, c.g, c.b)

def map_val(val, a, b, c, d):
    val = max(a, min(b, val))
    return c + (val - a) * (d - c) / (b - a)

def draw_rounded_petal(surf, tip_x, tip_y, petal_angle, petal_len, petal_w, color):
    # draw a soft elliptical petal using pygame ellipse rotated via surface
    petal_surf = pygame.Surface((petal_w * 2, petal_len * 2), pygame.SRCALPHA)
    darker = (max(0, color[0]-40), max(0, color[1]-40), max(0, color[2]-40), 255)
    pygame.draw.ellipse(petal_surf, (*color, 255),
                        (0, 0, petal_w * 2, petal_len * 2))
    pygame.draw.ellipse(petal_surf, darker,
                        (0, 0, petal_w * 2, petal_len * 2), 2)

    # rotate petal to the correct angle
    angle_deg = -math.degrees(petal_angle) - 90
    rotated = pygame.transform.rotate(petal_surf, angle_deg)

    # position: center the petal so its base is at tip
    cx = tip_x + math.cos(petal_angle) * petal_len
    cy = tip_y + math.sin(petal_angle) * petal_len
    rect = rotated.get_rect(center=(int(cx), int(cy)))
    surf.blit(rotated, rect)

def draw_flower(surf, cx, cy, stem_angle_deg, petal_color, leaf_color):
    stem_angle = math.radians(stem_angle_deg)
    tip_x = cx + math.sin(stem_angle) * STEM_LEN
    tip_y = cy - math.cos(stem_angle) * STEM_LEN

    # leaves
    leaf_base_x = cx + math.sin(stem_angle) * STEM_LEN * 0.55
    leaf_base_y = cy - math.cos(stem_angle) * STEM_LEN * 0.55
    perp     = stem_angle + math.pi / 2
    leaf_len = 70
    leaf_w   = 22

    for side in [-1, 1]:
        lx  = leaf_base_x + math.cos(perp) * side * leaf_w
        ly  = leaf_base_y + math.sin(perp) * side * leaf_w
        lx2 = leaf_base_x + math.sin(stem_angle) * leaf_len + math.cos(perp) * side * 10
        ly2 = leaf_base_y - math.cos(stem_angle) * leaf_len + math.sin(perp) * side * 10
        points = [
            (int(leaf_base_x), int(leaf_base_y)),
            (int(lx), int(ly)),
            (int(lx2), int(ly2)),
        ]
        pygame.draw.polygon(surf, leaf_color, points)
        pygame.draw.polygon(surf, (30, 100, 30), points, 1)

    # stem
    pygame.draw.line(surf, (60, 160, 60),
                     (int(cx), int(cy)), (int(tip_x), int(tip_y)), 6)

    # rounded petals
    num_petals = 8
    petal_len  = 52
    petal_w    = 22
    for i in range(num_petals):
        petal_angle = stem_angle + math.radians(i * 360 / num_petals)
        draw_rounded_petal(surf, tip_x, tip_y, petal_angle,
                           petal_len, petal_w, petal_color)

    # center circle
    pygame.draw.circle(surf, (255, 220, 60), (int(tip_x), int(tip_y)), 20)
    pygame.draw.circle(surf, (200, 160, 20), (int(tip_x), int(tip_y)), 20, 2)
    # small dots on center
    for i in range(6):
        a = math.radians(i * 60)
        dx = int(tip_x + math.cos(a) * 10)
        dy = int(tip_y + math.sin(a) * 10)
        pygame.draw.circle(surf, (180, 130, 10), (dx, dy), 3)

# ── state ────────────────────────────────────────────────────────
angle        = ANGLE_CENTER
force        = 0.0
needle_pos   = 0.0
locked_hue   = 0.0
settle_count = 0
petal_color  = hue_to_rgb(0)
leaf_color   = hue_to_rgb(120)
serial_buf   = ""

BG = (15, 20, 30)

running = True
while running:
    for event in pygame.event.get():
        if event.type == pygame.QUIT:
            running = False
        if event.type == pygame.KEYDOWN:
            if event.key == pygame.K_r:
                ser.write(b'r')   # tell Pico to reset unwrap offset
                print("Reset sent to Pico")

    # ── non-blocking serial read ─────────────────────────────────
    try:
        raw = ser.read(ser.in_waiting or 1)
        serial_buf += raw.decode(errors='ignore')
        while '\n' in serial_buf:
            line, serial_buf = serial_buf.split('\n', 1)
            line = line.strip()
            if ',' in line:
                parts = line.split(',')
                if len(parts) == 2:
                    try:
                        angle = float(parts[0])
                        force = float(parts[1]) - FORCE_ZERO
                    except:
                        pass
    except:
        pass

    # ── needle logic ─────────────────────────────────────────────
    force_norm = force / 50000.0
    force_norm = max(-3.0, min(3.0, force_norm))

    if abs(force) > FORCE_DEAD:
        needle_pos = (needle_pos + force_norm * NEEDLE_SPEED) % 1.0
        settle_count = 0
    else:
        settle_count += 1
        if settle_count >= SETTLE_FRAMES:
            locked_hue  = needle_pos * 360.0
            petal_color = hue_to_rgb(locked_hue)
            leaf_color  = hue_to_rgb((locked_hue + 120) % 360)

    # ── stem angle ───────────────────────────────────────────────
    stem_deg = map_val(angle, ANGLE_MIN, ANGLE_MAX, -90, 90)

    # ── draw ─────────────────────────────────────────────────────
    screen.fill(BG)
    pygame.draw.line(screen, (40, 80, 40),
                     (0, HEIGHT - 60), (WIDTH - 80, HEIGHT - 60), 3)

    draw_flower(screen, STEM_X, STEM_Y, stem_deg, petal_color, leaf_color)

    # rainbow bar
    screen.blit(rainbow_surf, (BAR_X, BAR_Y))
    pygame.draw.rect(screen, (200, 200, 200), (BAR_X, BAR_Y, BAR_W, BAR_H), 2)

    # needle
    needle_y = int(BAR_Y + needle_pos * BAR_H)
    pygame.draw.polygon(screen, (255, 255, 255), [
        (BAR_X - 14, needle_y),
        (BAR_X,      needle_y - 7),
        (BAR_X,      needle_y + 7),
    ])

    # labels
    bar_label = font.render("Color", True, (200, 200, 200))
    screen.blit(bar_label, (BAR_X - 10, BAR_Y - 30))

    hint1 = font_hint.render("Apply Force to Load Cell → Move Color Needle to Select Flower Color", True, (140, 140, 140))
    hint2 = font_hint.render("Release → Lock Flower Color",      True, (140, 140, 140))
    hint3 = font_hint.render("R = Reset Stem Angle",           True, (140, 140, 140))
    screen.blit(hint1, (20, 80))
    screen.blit(hint2, (20, 102))
    screen.blit(hint3, (20, 124))

    angle_label = font.render(f"Angle: {stem_deg:.1f}°", True, (180, 180, 180))
    force_label = font.render(f"Force: {force:.0f}",     True, (180, 180, 180))
    screen.blit(angle_label, (20, 20))
    screen.blit(force_label, (20, 48))

    pygame.display.flip()
    clock.tick(60)

ser.close()
pygame.quit()
sys.exit()