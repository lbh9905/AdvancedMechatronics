#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ── Pin definitions ──────────────────────────────────────────────────────────
// On the Pico 2W: SPI0, GP16=RX, GP18=SCK, GP19=TX, GP17=CS
#define SPI_PORT    spi_default
#define PIN_CS      PICO_DEFAULT_SPI_CSN_PIN   // GP17

// ── DAC channel select ────────────────────────────────────────────────────────
// The MCP4912 has two output channels: A and B
// bit 15 of the 16-bit word tells the chip which channel to write to
#define DAC_CHANNEL_A  0    // bit15 = 0 → channel A (VOUTA, pin 14)
#define DAC_CHANNEL_B  1    // bit15 = 1 → channel B (VOUTB, pin 10)

// ── Signal parameters ────────────────────────────────────────────────────────
#define SINE_FREQ_HZ        2       // 2 Hz sine wave on channel A
#define TRIANGLE_FREQ_HZ    1       // 1 Hz triangle wave on channel B
#define UPDATE_RATE_HZ      500     // 500 updates/sec (250x the 2Hz sine, well above 50x minimum)

// ── CS helpers (from assignment sample code) ──────────────────────────────────
// CS is "active low": CS=0 means chip is selected/listening, CS=1 means deselected
// The nop instructions are tiny delays to let the pin settle
static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 0);               // pull CS low = select the chip
    asm volatile("nop \n nop \n nop"); // FIXME
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop"); // FIXME
    gpio_put(cs_pin, 1);               // pull CS high = deselect the chip
    asm volatile("nop \n nop \n nop"); // FIXME
}

// ── writeDAC ─────────────────────────────────────────────────────────────────
// Sends a voltage value to one channel of the MCP4912 over SPI.
//
// Parameters:
//   channel = DAC_CHANNEL_A or DAC_CHANNEL_B
//   v       = voltage to output, as a float from 0.0 to 3.3
//
// The MCP4912 expects a 16-bit word split into 2 bytes:
//
//   Byte 0: [ A/B | BUF | GA | SHDN | D9 | D8 | D7 | D6 ]
//   Byte 1: [ D5  | D4  | D3 | D2   | D1 | D0 | X  | X  ]
//
//   A/B  = channel select (0=A, 1=B)
//   BUF  = voltage reference buffer (0=unbuffered)
//   GA   = gain select (1=1x gain, output = 0 to VREF = 0 to 3.3V)
//   SHDN = shutdown (1=active/on, 0=shutdown/off)
//   D9-D0 = 10-bit value representing the voltage
//   X    = unused bits
void writeDAC(int channel, float v) {
    uint8_t data[2];

    // Start byte 0 with config bits:
    // 0b01110000 =
    //   bit7 (A/B)  = 0 (channel A for now, OR'd in below)
    //   bit6 (BUF)  = 1 (buffered)
    //   bit5 (GA)   = 1 (1x gain, output = 0 to 3.3V)
    //   bit4 (SHDN) = 1 (active, NOT shutdown)
    //   bits 3-0    = 0 (upper data bits, filled in below)
    data[0] = 0b01111000;

    // OR in the channel select bit at bit position 7
    // (channel & 0b1) isolates just the lowest bit of channel
    // << 7 shifts it up to bit 15 of the full 16-bit word
    data[0] = data[0] | ((channel & 0b1) << 7);

    // Convert float voltage (0.0 to 3.3V) to 10-bit integer (0 to 1023)
    // Example: 1.65V → 1.65/3.3*1023 = 511
    uint16_t myV = v / 3.3 * 1023;

    // Put upper 4 bits of myV (bits 9-6) into lower nibble of data[0]
    // myV>>6 shifts right by 6, bringing bits 9-6 down to positions 3-0
    // &0b00001111 masks to only affect lower 4 bits
    data[0] = data[0] | ((myV >> 6) & 0b00001111);

    // Put lower 6 bits of myV (bits 5-0) into upper 6 bits of data[1]
    // myV<<2 shifts left by 2, putting bits 5-0 into positions 7-2
    // &0xFF masks to a single byte
    data[1] = 0b11111100;

    // Send the 2 bytes over SPI
    cs_select(PIN_CS);
    spi_write_blocking(SPI_PORT, data, 2);
    cs_deselect(PIN_CS);
}

// ── Waveform generators ──────────────────────────────────────────────────────

// Returns voltage (0.0 to 3.3V) for a sine wave at 'freq' Hz at time t seconds
// sinf() gives -1.0 to +1.0, we shift and scale to 0.0 to 3.3V
float sine_voltage(float t, float freq) {
    float s = sinf(2.0f * (float)M_PI * freq * t);
    return (s + 1.0f) / 2.0f * 3.3f;
}

// Returns voltage (0.0 to 3.3V) for a triangle wave at 'freq' Hz at time t seconds
// Ramps linearly up then down over one period
float triangle_voltage(float t, float freq) {
    float phase = fmodf(t * freq, 1.0f);  // 0.0 to 1.0 over one period
    float tri;
    if (phase < 0.5f) {
        tri = phase * 2.0f;               // ramp up:   0.0 → 1.0
    } else {
        tri = 2.0f - phase * 2.0f;        // ramp down: 1.0 → 0.0
    }
    return tri * 3.3f;
}

// ── Main ─────────────────────────────────────────────────────────────────────
int main() {
    stdio_init_all();

    // SPI init - MCP4912 requires Mode 0,0: clock idles low, data sampled on rising edge
    spi_init(spi_default, 1000 * 1000);
    spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN,  GPIO_FUNC_SPI);  // GP16
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);  // GP18
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN,  GPIO_FUNC_SPI);  // GP19

    // CS pin - regular GPIO, manually controlled
    gpio_init(PIN_CS);
    gpio_set_dir(PIN_CS, GPIO_OUT);
    gpio_put(PIN_CS, 1);  // start high (deselected)

    // ── Step 1 test: output fixed 1.65V on both channels ─────────────────────
    // Uncomment to verify wiring works before running waveform loop.
    // You should see ~1.65V DC on oscilloscope on both VOUTA and VOUTB.
    //
     writeDAC(DAC_CHANNEL_A, 3.3);
     writeDAC(DAC_CHANNEL_B, 3.3);
     while (1) { tight_loop_contents(); }

    // ── Step 2: waveform loop ─────────────────────────────────────────────────
    const uint32_t period_us = 1000000 / UPDATE_RATE_HZ;
    float t = 0.0f;
    const float dt = 1.0f / UPDATE_RATE_HZ;

    while (1) {
        writeDAC(DAC_CHANNEL_A, sine_voltage(t, SINE_FREQ_HZ));
        writeDAC(DAC_CHANNEL_B, triangle_voltage(t, TRIANGLE_FREQ_HZ));
        t += dt;
        sleep_us(period_us);
    }

    return 0;
}
