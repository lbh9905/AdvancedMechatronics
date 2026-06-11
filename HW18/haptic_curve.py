import numpy as np
import matplotlib.pyplot as plt

# ── paddle range ─────────────────────────────────────────────────
angle_min = 226.0
angle_max = 419.0
angles = np.linspace(angle_min, angle_max, 1000)

# ── bump function ─────────────────────────────────────────────────
# each bump is a gaussian (bell curve) — smooth rise and fall
# center: where the bump peaks
# width:  how wide the blade of grass is (smaller = sharper snap)
# height: how strong the resistance is (normalized to +/-1)
def bump(x, center, width, height):
    return height * np.exp(-0.5 * ((x - center) / width) ** 2)

# ── 4 evenly spaced grass blades ─────────────────────────────────
centers = np.linspace(332.5, angle_max - 20, 4)
width   = 12.0    # degrees wide — tweak to make sharper or softer
height  = 1.0     # normalized max force

force = np.zeros_like(angles)
for c in centers:
    force += bump(angles, c, width, height)

# normalize to -1 to +1 range
force = force / np.max(np.abs(force))

# ── plot ──────────────────────────────────────────────────────────
fig, ax = plt.subplots(figsize=(10, 5))

ax.plot(angles, force, color='#4CAF50', linewidth=2.5, label='Desired force')
ax.axhline(0, color='gray', linewidth=0.8, linestyle='--')

# mark each grass blade
for i, c in enumerate(centers):
    ax.axvline(c, color='#8BC34A', linewidth=1, linestyle=':', alpha=0.6)
    ax.text(c, 1.05, f'Blade {i+1}', ha='center', fontsize=9, color='#558B2F')

# shade the grass region
ax.fill_between(angles, force, 0, where=(force > 0),
                alpha=0.15, color='#4CAF50', label='Resistance zone')

ax.set_xlabel('Paddle angle (degrees)', fontsize=12)
ax.set_ylabel('Desired force (normalized, -1 to +1)', fontsize=12)
ax.set_title('Haptic effect — grass bumps\nFlower pushing through blades of grass', fontsize=13)
ax.set_xlim(angle_min, angle_max)
ax.set_ylim(-0.2, 1.3)
ax.legend(fontsize=10)
ax.grid(True, alpha=0.3)

# add paddle range labels
ax.text(angle_min + 2, -0.15, 'Left end stop', fontsize=9, color='gray')
ax.text(angle_max - 2, -0.15, 'Right end stop', fontsize=9, color='gray', ha='right')

plt.tight_layout()
plt.savefig('haptic_curve.png', dpi=150)
plt.show()
print("Saved to haptic_curve.png")