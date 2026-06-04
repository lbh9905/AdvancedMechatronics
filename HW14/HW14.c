// HW14 - Force Sensor Reading with HX711
// Pico 2W reads force data from HX711 and sends it to Python over USB serial
//
// WIRING:
//   HX711 VCC  -> Pico 3.3V (pin 36)
//   HX711 GND  -> Pico GND
//   HX711 DT   -> Pico GP14 (pin 19)  <- data line, we READ this
//   HX711 SCK  -> Pico GP15 (pin 20)  <- clock line, we PULSE this

#include <stdio.h>
#include "pico/stdlib.h"

// ── Pin definitions ──────────────────────────────────────────────
#define HX711_DT  14   // Data pin  (GP14): HX711 signals when data is ready
                       //   and sends us bits one at a time on this wire
#define HX711_SCK 15   // Clock pin (GP15): we pulse this to ask for each bit

// ── IIR filter weight ────────────────────────────────────────────
// Alpha controls how much smoothing we do.
// Think of it like a mixing knob:
//   alpha = 0.1  → 10% new reading, 90% old filtered value  (very smooth, slow)
//   alpha = 0.5  → 50/50 mix                                (moderate)
//   alpha = 0.9  → 90% new reading, 10% old               (fast but noisy)
//
// The assignment says noise appears at 25-30 Hz. We sample at 80 Hz.
// A small alpha (0.1) cuts high-frequency noise well.
#define ALPHA 0.05f


// ── Function: init_hx711 ─────────────────────────────────────────
// Sets up the two GPIO pins we need to talk to the HX711.
// SCK is an OUTPUT because WE control the clock pulses.
// DT  is an INPUT  because the HX711 controls that line; we just read it.
void init_hx711() {
    // Set up the clock pin as output, start it LOW (idle state)
    gpio_init(HX711_SCK);
    gpio_set_dir(HX711_SCK, GPIO_OUT);
    gpio_put(HX711_SCK, 0);  // clock starts LOW

    // Set up the data pin as input so we can read what the HX711 sends
    gpio_init(HX711_DT);
    gpio_set_dir(HX711_DT, GPIO_IN);
}


// ── Function: read_hx711 ─────────────────────────────────────────
// Reads one 24-bit sample from the HX711.
// Returns a signed 32-bit integer (can be positive or negative).
//
// HOW IT WORKS (bit-banging):
//   The HX711 holds DT HIGH while it is busy converting.
//   When DT goes LOW, it means "I have a reading ready, come get it!"
//   We then pulse SCK 25 times:
//     - Pulses 1-24: after each pulse, read one bit from DT
//     - Pulse 25:    tells HX711 to use gain=128 for the next reading
//   The 24 bits we collect form the raw force value.
int32_t read_hx711() {
    uint32_t raw = 0;  // will hold the 24 bits as we collect them

    // ── Step 1: Wait for the HX711 to be ready ───────────────────
    // DT is HIGH while the chip is still sampling.
    // We just sit here in a loop until DT goes LOW.
    while (gpio_get(HX711_DT) == 1) {
        tight_loop_contents();  // Pico SDK way of saying "wait, do nothing"
    }

    // ── Step 2: Read 24 bits, one per clock pulse ─────────────────
    // We loop 24 times. Each time:
    //   1. Pulse SCK HIGH  (rising edge tells HX711 to put next bit on DT)
    //   2. Read DT         (that bit is our data)
    //   3. Shift it into our 'raw' variable
    //   4. Pull SCK LOW    (get ready for the next pulse)
    for (int i = 0; i < 24; i++) {
        gpio_put(HX711_SCK, 1);       // clock goes HIGH
        sleep_us(1);                  // tiny pause so HX711 has time to respond

        // Shift 'raw' left by 1 to make room, then OR in the new bit
        // Example after 3 bits: raw = 0b___101  (bit2, bit1, bit0 collected so far)
        raw = (raw << 1) | gpio_get(HX711_DT);

        gpio_put(HX711_SCK, 0);       // clock goes LOW
        sleep_us(1);
    }

    // ── Step 3: 25th pulse — sets gain=128 for next reading ───────
    gpio_put(HX711_SCK, 1);
    sleep_us(1);
    gpio_put(HX711_SCK, 0);
    sleep_us(1);

    // ── Step 4: Sign extension ────────────────────────────────────
    // The 24 bits we got represent a number that can be positive OR negative.
    // But our variable 'raw' is unsigned (only positive) right now.
    //
    // In two's complement (how computers store negative numbers),
    // if the top bit (bit 23) is 1, the number is actually negative.
    //
    // To convert correctly to a signed 32-bit int, we fill the upper
    // 8 bits with 1s. This is called "sign extension."
    //
    // Example: 24-bit value 0xC00000 = negative in 24-bit world
    //   Without sign extend: looks like +12,582,912
    //   With sign extend:    correctly becomes -4,194,304
    if (raw & 0x800000) {
        raw |= 0xFF000000;  // fill upper 8 bits with 1s
    }

    return (int32_t)raw;  // cast to signed and return
}


// ── Main ─────────────────────────────────────────────────────────
int main() {
    stdio_init_all();   // sets up USB serial so we can print to computer
    init_hx711();       // set up our two GPIO pins

    // IIR filter state — starts at 0, will update with each reading
    // (float so we can do fractional math for the weighted average)
    float filtered = 0.0f;
    int first_reading = 1;  // flag so we seed the filter on the very first sample

    // ── Wait for Python to send us a number of samples to collect ─
    // Python will send a number like "200\n" over the serial port.
    // We read it, then collect exactly that many samples.
    int num_samples = 0;
    while (num_samples <= 0) {
        scanf("%d", &num_samples);  // blocks here until Python sends something
    }

    // ── Collect the requested number of samples ───────────────────
    for (int i = 0; i < num_samples; i++) {

        // Record the time RIGHT BEFORE reading, in milliseconds
        // This gives us an accurate timestamp for each sample
        uint32_t timestamp_ms = to_ms_since_boot(get_absolute_time());

        // Get a raw reading from the HX711
        int32_t raw = read_hx711();

        // ── IIR filter ────────────────────────────────────────────
        // filtered = alpha * new_value + (1 - alpha) * old_filtered
        //
        // On the very first sample, seed the filter with the raw value
        // so it doesn't start from 0 and take a long time to catch up.
        if (first_reading) {
            filtered = (float)raw;
            first_reading = 0;
        } else {
            filtered = ALPHA * (float)raw + (1.0f - ALPHA) * filtered;
        }

        // ── Send data to Python ───────────────────────────────────
        // Format: raw_value,filtered_value,timestamp_ms
        // Python will split on the commas to get each field.
        printf("%d,%.1f,%lu\n", raw, filtered, (unsigned long)timestamp_ms);
    }

    // All done — Python will stop reading after it gets num_samples lines
    return 0;
}