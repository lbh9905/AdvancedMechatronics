import serial
import serial.tools.list_ports
import matplotlib.pyplot as plt

# ── find the Pico's COM port automatically ──────────────────────
def find_pico_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if 'USB' in p.description or 'Pico' in p.description:
            return p.device
    return None

PORT = find_pico_port()
if PORT is None:
    print("Could not find Pico! Check USB connection.")
    exit()

BAUD = 115200
print(f"Connecting to Pico on {PORT}...")
ser = serial.Serial(PORT, BAUD, timeout=10)

# ── wait for ready message ──────────────────────────────────────
print("Waiting for Pico to be ready...")
while True:
    line = ser.readline().decode().strip()
    print(line)
    if "Ready" in line:
        break

# ── send 'a' to start a run ─────────────────────────────────────
input("Press Enter to start a run...")
ser.write(b'a')
print("Run started!")

# ── wait for data ───────────────────────────────────────────────
print("Waiting for data...")
while True:
    line = ser.readline().decode().strip()
    if line == "DATA START":
        break

# ── read all data lines ─────────────────────────────────────────
desired = []
actual  = []

while True:
    line = ser.readline().decode().strip()
    if line == "DATA END":
        break
    parts = line.split(',')
    if len(parts) == 2:
        desired.append(float(parts[0]))
        actual.append(float(parts[1]))

ser.close()
print(f"Received {len(desired)} samples. Plotting...")

# ── plot ────────────────────────────────────────────────────────
time_ms = [i for i in range(len(desired))]   # each sample = 1ms

plt.figure(figsize=(10, 5))
plt.plot(time_ms, desired, label='Desired (mA)', linestyle='--', color='blue')
plt.plot(time_ms, actual,  label='Actual (mA)',  color='orange')
plt.xlabel('Time (ms)')
plt.ylabel('Current (mA)')
plt.title('PI Current Controller — Desired vs Actual')
plt.legend()
plt.grid(True)
plt.tight_layout()
plt.show()