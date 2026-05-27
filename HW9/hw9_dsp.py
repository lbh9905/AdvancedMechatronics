# HW9: the main purpose of this assignment is to know how to load in data, 
# graph it, identify its peaks and overall behavior to properly assign 
# filters and then apply those filters to the data

#more detailed Claude version:
#Yes, exactly! You've summarized it perfectly. The full picture is:
#**1. Load & visualize the data** — read CSVs, plot signal vs time so you can see what you're working with
#**2. Analyze the frequency content** — use the FFT to convert from "signal vs time" to "signal vs frequency"
# so you can actually *see* where your signal is and where the noise is
#**3. Choose appropriate filter parameters** — this is the engineering judgment part. Based on the FFT you decide:
#- Where does my real signal end and noise begin?
#- How aggressively do I need to filter?
#**4. Apply the filters and evaluate** — run MAF, IIR, and FIR and compare the before/after FFTs to confirm the noise was actually removed
#The bigger lesson your professor is teaching is the **difference between the three filter types:**
#- **MAF** — simple and easy to code, but not very precise
#- **IIR** — also simple, reacts continuously, but never fully settles
#- **FIR sinc** — most precise cutoff, but more complex to design

#In real mechatronics/engineering work this comes up constantly — any sensor (IMU, encoder, load cell) gives you noisy data 
# and you need to clean it up before using it to control something. That's why your professor wants you to do this in Python
# first before implementing it in C on a microcontroller.

import csv
import numpy as np
import matplotlib.pyplot as plt

import os
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# ================================================================
#  python_csv  –  based on professor's python_csv.py sample
#  Reads a two-column CSV file (time in column 0, value in column 1)
#  and returns them as two separate lists, just like the sample code.
# ================================================================
def python_csv(filename):
    t     = []      # column 0  – time values
    data1 = []      # column 1  – signal values

    with open(filename, newline='') as f:
        # csv.reader splits each line into a list of strings
        reader = csv.reader(f)
        for row in reader:
            if len(row) >= 2:           # make sure the row has both columns
                try:
                    t.append(float(row[0]))       # leftmost column  → time
                    data1.append(float(row[1]))   # second column    → signal
                except ValueError:
                    pass    # skip any header/label rows that can't be converted
    return t, data1


# ================================================================
#  python_fft  –  based on professor's python_fft.py sample
#  Takes a signal and its sample rate, returns the one-sided
#  frequency array (frq) and magnitude array (Y) for plotting.
#
#  The professor's code does:
#      Fs = sample rate
#      Ts = 1/Fs  (sampling interval)
#      ts = time vector (same length as signal)
#      y  = the signal data
#      n  = len(y)
#      k  = np.arange(n)
#      T  = n/Fs
#      frq = k/T              → two-sided frequency range
#      frq = frq[range(n//2)] → one-sided frequency range
#      Y   = np.fft.fft(y)/n  → FFT computing and normalisation
#      Y   = Y[range(n//2)]   → keep only the one-sided half
# ================================================================
def python_fft(y, Fs):
    Ts  = 1.0 / Fs              # sampling interval (seconds per sample)
    n   = len(y)                # length of the signal
    k   = np.arange(n)          # array [0, 1, 2, ..., n-1]
    T   = n / Fs                # total duration of the signal in seconds
    frq = k / T                 # two-sided frequency range (Hz)
    frq = frq[range(n // 2)]    # one-sided frequency range (keep first half)
    Y   = np.fft.fft(y) / n    # compute FFT and normalise by number of points
    Y   = Y[range(n // 2)]     # keep only the one-sided (positive) frequencies
    return frq, np.abs(Y)       # return frequencies and their magnitudes


# ================================================================
#  python_plot  –  based on professor's python_plot.py sample
#  Plots signal vs time AND its FFT as two subplots on one figure,
#  using ax1/ax2 just like the professor's sample code.
# ================================================================
def python_plot(name, t, y, Fs):
    # compute the FFT using our python_fft function above
    frq, Y = python_fft(y, Fs)

    # create a figure with 2 rows, 1 column of subplots (same as professor's sample)
    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    fig.suptitle(f'{name}  –  Signal & FFT')

    # top subplot: signal vs time (blue line, 'b')
    ax1.plot(t, y, 'b')
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Amplitude')
    ax1.set_title('Signal vs Time')

    # bottom subplot: FFT on a log-log scale (blue line, 'b')
    ax2.loglog(frq, Y, 'b')
    ax2.set_xlabel('Freq [Hz]')
    ax2.set_ylabel('|Y(freq)|')
    ax2.set_title('FFT')

    plt.tight_layout()
    plt.savefig(f'{name}_raw.png', dpi=120)   # save the figure as a PNG
    plt.show()
    plt.close()


# ================================================================
#  PART 3  –  Calculate sample rate from the time column
#
#  Formula from the assignment:
#      sample rate = number of data points / total time of samples
#  The -1 index trick (t[-1]) gives us the last element of the list.
# ================================================================
def get_sample_rate(t):
    num_points = len(t)                 # total number of data points
    total_time = t[-1] - t[0]          # t[-1] is the last value in the list
    Fs = num_points / total_time        # sample rate in Hz (samples per second)
    return Fs


# ================================================================
#  PART 5  –  Moving Average Filter (MAF)
#
#  The assignment says: write a loop that averages the last X data
#  points and saves the result in a new list.
#  If we haven't collected X points yet, we treat the missing
#  earlier values as 0 (the buffer starts full of zeros).
# ================================================================
def moving_average_filter(signal, X):
    filtered = []                           # new list to store smoothed values

    for i in range(len(signal)):
        if i < X:
            # not enough past samples yet – pad the front with zeros
            window = [0.0] * (X - i) + signal[:i]
        else:
            # grab the last X samples from the signal
            window = signal[i - X : i]

        # average all values in the window and add to our filtered list
        filtered.append(sum(window) / X)

    return filtered


def plot_maf(name, t, sig, sig_filtered, X, Fs):
    # compute FFTs for both the raw and filtered signals so we can compare them
    frq_raw,  mag_raw  = python_fft(sig,          Fs)
    frq_filt, mag_filt = python_fft(sig_filtered, Fs)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    fig.suptitle(f'{name}  –  MAF  (X = {X} points)')

    # top plot: unfiltered in black, filtered in red (as required by assignment)
    ax1.plot(t, sig,          'k', label='Unfiltered')
    ax1.plot(t, sig_filtered, 'r', label=f'MAF  X={X}')
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Amplitude')
    ax1.set_title(f'Signal vs Time  –  averaged over {X} points')   # title includes X
    ax1.legend()

    # bottom plot: compare FFTs to see the low-pass effect
    ax2.loglog(frq_raw,  mag_raw,  'k', label='Unfiltered FFT')
    ax2.loglog(frq_filt, mag_filt, 'r', label='Filtered FFT')
    ax2.set_xlabel('Freq [Hz]')
    ax2.set_ylabel('|Y(freq)|')
    ax2.set_title('FFT Comparison')
    ax2.legend()

    plt.tight_layout()
    plt.savefig(f'{name}_MAF_X{X}.png', dpi=120)
    plt.show()
    plt.close()


# ================================================================
#  PART 6  –  IIR Filter (Infinite Impulse Response)
#
#  The assignment formula:
#      new_average[i] = A * new_average[i-1] + B * signal[i]
#  where A + B = 1.
#
#  A large A (e.g. 0.95) = heavy smoothing, slow to react
#  A large B (e.g. 0.5)  = less smoothing, reacts quickly
# ================================================================
def iir_filter(signal, A, B):
    # initialise the output list with zeros, same length as input
    new_average = [0.0] * len(signal)

    # seed the first value with the first sample (nothing to average yet)
    new_average[0] = signal[0]

    for i in range(1, len(signal)):
        # this is exactly the formula from the assignment
        new_average[i] = A * new_average[i - 1] + B * signal[i]

    return new_average


def plot_iir(name, t, sig, sig_filtered, A, B, Fs):
    frq_raw,  mag_raw  = python_fft(sig,          Fs)
    frq_filt, mag_filt = python_fft(sig_filtered, Fs)

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    fig.suptitle(f'{name}  –  IIR  (A={A}, B={B})')

    # unfiltered in black, filtered in red; A and B shown in title
    ax1.plot(t, sig,          'k', label='Unfiltered')
    ax1.plot(t, sig_filtered, 'r', label=f'IIR  A={A}  B={B}')
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Amplitude')
    ax1.set_title(f'Signal vs Time  –  A={A}, B={B}')   # title includes A and B
    ax1.legend()

    ax2.loglog(frq_raw,  mag_raw,  'k', label='Unfiltered FFT')
    ax2.loglog(frq_filt, mag_filt, 'r', label='Filtered FFT')
    ax2.set_xlabel('Freq [Hz]')
    ax2.set_ylabel('|Y(freq)|')
    ax2.set_title('FFT Comparison')
    ax2.legend()

    plt.tight_layout()
    plt.savefig(f'{name}_IIR_A{A}_B{B}.png', dpi=120)
    plt.show()
    plt.close()


# ================================================================
#  PART 7  –  FIR Filter with windowed-sinc weights
#
#  Instead of equal weights (MAF) or a single decaying weight (IIR),
#  a full FIR filter uses unique weights for each of the X samples.
#  The weights are chosen to create a specific frequency response.
#
#  Steps:
#   1. Pick a cutoff frequency and number of taps (X)
#   2. Generate sinc weights for that cutoff
#   3. Multiply by a window function to reduce ringing
#   4. Normalise so the weights sum to 1 (DC gain = 1)
#   5. Apply by looping over the signal (or using convolution)
# ================================================================
def make_sinc_weights(cutoff_hz, bandwidth_hz, num_taps, Fs, window='hamming'):
    fc = cutoff_hz / Fs     # normalised cutoff frequency (must be between 0 and 0.5)
    M  = num_taps - 1       # filter order

    h = np.zeros(num_taps)  # array to hold the sinc weights

    for i in range(num_taps):
        if i == M // 2:
            # centre tap of the sinc – avoid divide-by-zero with the limit value
            h[i] = 2 * fc
        else:
            # standard sinc formula shifted to be centred at M/2
            h[i] = np.sin(2 * np.pi * fc * (i - M / 2)) / (np.pi * (i - M / 2))

    # apply a window to reduce spectral leakage / ringing at the cutoff
    if window == 'hamming':
        w = np.hamming(num_taps)    # good general-purpose window
    elif window == 'hanning':
        w = np.hanning(num_taps)    # similar to hamming, slightly different shape
    elif window == 'blackman':
        w = np.blackman(num_taps)   # better stopband, wider transition band
    else:
        w = np.ones(num_taps)       # rectangular window (no windowing)

    h = h * w               # multiply sinc by the window
    h = h / np.sum(h)       # normalise so all weights sum to 1 (DC gain = 1)
    return h


def fir_filter(signal, weights):
    # np.convolve slides the weights across the signal – same as the loop in the assignment
    # mode='same' keeps the output the same length as the input
    return list(np.convolve(signal, weights, mode='same'))


def plot_fir(name, t, sig, sig_filtered, num_taps, window, cutoff_hz, bandwidth_hz, Fs):
    frq_raw,  mag_raw  = python_fft(sig,          Fs)
    frq_filt, mag_filt = python_fft(sig_filtered, Fs)

    # title includes: how many taps, window type, cutoff, and bandwidth
    title_str = (f'{name}  –  FIR sinc | taps={num_taps}, window={window}, '
                 f'cutoff={cutoff_hz} Hz, BW={bandwidth_hz} Hz')

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 6))
    fig.suptitle(title_str, fontsize=9)

    ax1.plot(t, sig,          'k', label='Unfiltered')
    ax1.plot(t, sig_filtered, 'r', label='FIR filtered')
    ax1.set_xlabel('Time [s]')
    ax1.set_ylabel('Amplitude')
    ax1.set_title('Signal vs Time')
    ax1.legend()

    ax2.loglog(frq_raw,  mag_raw,  'k', label='Unfiltered FFT')
    ax2.loglog(frq_filt, mag_filt, 'r', label='Filtered FFT')
    ax2.set_xlabel('Freq [Hz]')
    ax2.set_ylabel('|Y(freq)|')
    ax2.set_title('FFT Comparison')
    ax2.legend()

    plt.tight_layout()
    plt.savefig(f'{name}_FIR_sinc_{cutoff_hz}Hz.png', dpi=120)
    plt.show()
    plt.close()


# ================================================================
#  MAIN  –  run all parts for each of the four CSV files
#
#  IMPORTANT: after running Part 4 and looking at the FFTs,
#  come back and tune the values below for each signal before
#  running Parts 5-7.  The starting values here are reasonable
#  guesses but may not be "best" for your specific data.
# ================================================================

# Part 5 tuning: number of points to average in the MAF
# Larger X → smoother signal, but more lag introduced
MAF_X = {
    'sigA': 10,
    'sigB': 15,
    'sigC': 5,     # sigC is a square wave – keep X small
    'sigD': 20,
}

# Part 6 tuning: IIR weights A and B  (A + B must always equal 1)
# Large A → heavy smoothing (noisy signals)
# Large B → light smoothing (less noisy signals)
IIR_PARAMS = {
    # signal : (A,    B)
    'sigA':   (0.92, 0.08),
    'sigB':   (0.85, 0.15),
    'sigC':   (0.60, 0.40),
    'sigD':   (0.90, 0.10),
}

# Part 7 tuning: FIR sinc parameters
# Choose cutoff based on where the noise starts in the FFT plot
FIR_PARAMS = {
    # signal : (cutoff_hz, bandwidth_hz, num_taps, window)
    'sigA':   (50,  20, 101, 'hamming'),
    'sigB':   (100, 50, 101, 'hamming'),
    'sigC':   (200, 50,  51, 'hanning'),
    'sigD':   (30,  10, 201, 'blackman'),
}

CSV_FILES = ['sigA.csv', 'sigB.csv', 'sigC.csv', 'sigD.csv']

for csv_file in CSV_FILES:
    name = csv_file.replace('.csv', '')     # e.g. 'sigA.csv' → 'sigA'
    print(f'\n=== Processing {name} ===')

    # ── Part 3: load data and compute sample rate ──────────────────
    t, data1 = python_csv(csv_file)         # read time and signal from CSV
    Fs = get_sample_rate(t)                 # compute sample rate in Hz
    print(f'  Sample rate : {Fs:.1f} Hz')
    print(f'  Data points : {len(t)}')
    print(f'  Duration    : {t[-1] - t[0]:.3f} s')

    # ── Part 4: plot raw signal and FFT ───────────────────────────
    python_plot(name, t, data1, Fs)

    # ── Part 5: Moving Average Filter ─────────────────────────────
    X            = MAF_X[name]
    sig_maf      = moving_average_filter(data1, X)
    plot_maf(name, t, data1, sig_maf, X, Fs)

    # ── Part 6: IIR Filter ────────────────────────────────────────
    A, B         = IIR_PARAMS[name]
    sig_iir      = iir_filter(data1, A, B)
    plot_iir(name, t, data1, sig_iir, A, B, Fs)

    # ── Part 7: FIR sinc Filter ───────────────────────────────────
    cutoff, bw, taps, win = FIR_PARAMS[name]
    weights      = make_sinc_weights(cutoff, bw, taps, Fs, window=win)
    sig_fir      = fir_filter(data1, weights)
    plot_fir(name, t, data1, sig_fir, taps, win, cutoff, bw, Fs)

print('\nAll done! PNG files have been saved in the same folder as this script.')
