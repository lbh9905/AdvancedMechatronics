# HW14 - Python data collection and plotting script
#
# WHAT THIS DOES:
#   1. Opens a serial connection to the Pico over USB
#   2. Sends the number of samples we want to collect
#   3. Reads back each line of data (raw, filtered, timestamp)
#   4. Plots raw vs filtered force over time
#   5. Takes an FFT of both to show which frequencies are present

import serial          # for talking to the Pico over USB
import time
import numpy as np     # for FFT math
import matplotlib.pyplot as plt   # for plotting

# ── Settings ─────────────────────────────────────────────────────
PORT = 'COM3'          # Windows: change to your port e.g. 'COM4'
                       # Mac/Linux: something like '/dev/ttyACM0'
BAUD = 115200          # must match the Pico's serial speed
NUM_SAMPLES = 800      # how many samples to collect
                       # at 80Hz, 800 samples = 10 seconds of data

# ── Open serial connection ────────────────────────────────────────
print(f"Connecting to Pico on {PORT}...")
ser = serial.Serial(PORT, BAUD, timeout=10)
time.sleep(2)          # give the Pico a moment to finish booting

# ── Send the number of samples ────────────────────────────────────
# The Pico is sitting in a loop waiting for us to send this number.
# We send it as a string with a newline at the end.
print(f"Requesting {NUM_SAMPLES} samples...")
ser.write(f"{NUM_SAMPLES}\n".encode())

# ── Read back the data ────────────────────────────────────────────
# Each line from the Pico looks like:  838123,837901.5,1042
# which is:  raw_value , filtered_value , timestamp_ms
raw_values      = []
filtered_values = []
timestamps_ms   = []

print("Collecting data... press on the sensor now!")
for i in range(NUM_SAMPLES):
    line = ser.readline().decode().strip()   # read one line, remove whitespace
    if line:
        parts = line.split(',')              # split on the comma
        if len(parts) == 3:
            raw_values.append(int(parts[0]))
            filtered_values.append(float(parts[1]))
            timestamps_ms.append(int(parts[2]))
print("Done collecting! Processing data...")

ser.close()
print(f"Got {len(raw_values)} samples. Plotting...")

# ── Convert timestamps to seconds (easier to read on a plot) ──────
t = np.array(timestamps_ms)
t = (t - t[0]) / 1000.0    # subtract start time, convert ms → seconds

raw  = np.array(raw_values)
filt = np.array(filtered_values)

# ── Plot 1: Raw vs Filtered over time ─────────────────────────────
fig, axes = plt.subplots(2, 1, figsize=(10, 8))

axes[0].set_title("Force Sensor — Raw vs IIR Filtered (Time Domain)")
axes[0].plot(t, raw,  label="Raw",          alpha=0.6, color='steelblue')
axes[0].plot(t, filt, label="IIR Filtered", linewidth=2, color='orange')
axes[0].set_xlabel("Time (seconds)")
axes[0].set_ylabel("HX711 ADC value")
axes[0].legend()
axes[0].grid(True)

# ── FFT calculation ───────────────────────────────────────────────
# The FFT tells us WHICH FREQUENCIES are present in the signal.
#
# IMPORTANT: Before taking the FFT, we subtract the mean (average value)
# from the signal. This is called "removing the DC offset."
#
# Why? Our raw signal sits around 2,564,000 — a huge number.
# If we don't subtract it, the FFT sees one giant spike at 0 Hz
# (representing that constant average value) which dwarfs everything
# else and makes all the actual frequency content invisible.
#
# After subtracting the mean, the signal wiggles around zero,
# and the FFT can clearly show the noise frequencies.
#
# Sampling rate = 80 Hz → Nyquist frequency = 40 Hz
# (We can only detect frequencies up to half the sample rate)

N  = len(raw)           # number of samples
Fs = 80.0               # sampling rate in Hz

freqs = np.fft.rfftfreq(N, d=1.0/Fs)   # x-axis: frequency values in Hz

# Subtract the mean from each signal before FFT (removes DC offset)
raw_centered  = raw  - np.mean(raw)
filt_centered = filt - np.mean(filt)

fft_raw  = np.abs(np.fft.rfft(raw_centered))   # FFT of centered raw signal
fft_filt = np.abs(np.fft.rfft(filt_centered))  # FFT of centered filtered signal

# ── Plot 2: FFT of raw vs filtered ────────────────────────────────
axes[1].set_title("FFT — Which Frequencies Are Present")
axes[1].plot(freqs, fft_raw,  label="Raw FFT",          alpha=0.6, color='steelblue')
axes[1].plot(freqs, fft_filt, label="IIR Filtered FFT", linewidth=2, color='orange')
axes[1].set_xlabel("Frequency (Hz)")
axes[1].set_ylabel("Magnitude")
axes[1].set_xlim([0, 40])   # only show up to Nyquist (40 Hz)
axes[1].legend()
axes[1].grid(True)

# Highlight the 25-30 Hz noise region the professor mentioned
axes[1].axvspan(25, 30, alpha=0.15, color='red', label='Noise region (25-30 Hz)')
axes[1].legend()

plt.tight_layout()
plt.savefig("HW14_plot.png", dpi=150)   # save image for Canvas submission
plt.show()
print("Plot saved as HW14_plot.png")