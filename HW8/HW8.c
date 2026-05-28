// =============================================================================
// HW8 - External SPI RAM Sine Wave Generator
// =============================================================================
// Purpose:
//   This program uses the 23K256 external SPI RAM chip to store 1000 floats
//   that represent one full cycle of a sine wave (0V to 3.3V). During
//   initialization, we calculate the sine values, convert them to 16-bit DAC
//   values using bitshifting, and write them into the RAM chip. Then, in the
//   infinite loop, we read two bytes back from RAM at a time and send them
//   directly to the MCP4912 DAC to produce a 1Hz sine wave on the oscilloscope.
//
// Wiring:
//   23K256 RAM:
//     CS   (pin 1) -> GP15
//     SO   (pin 2) -> GP16 (MISO)
//     Vss  (pin 4) -> GND
//     SI   (pin 5) -> GP19 (MOSI)
//     SCK  (pin 6) -> GP18 (SCK)
//     HOLD (pin 7) -> 3.3V
//     Vcc  (pin 8) -> 3.3V
//
//   MCP4912 DAC (unchanged from HW7):
//     VDD   (pin 1)  -> 3.3V
//     CS    (pin 3)  -> GP17
//     SCK   (pin 4)  -> GP18
//     SDI   (pin 5)  -> GP19 (MOSI)
//     LDAC  (pin 8)  -> GND
//     SHDN  (pin 9)  -> 3.3V
//     VSS   (pin 12) -> GND
//     VREFA (pin 13) -> 3.3V
//     VOUTA (pin 14) -> Oscilloscope
// =============================================================================

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

// ── Pin definitions ───────────────────────────────────────────────────────────
#define SPI_PORT    spi_default
#define PIN_CS_DAC  PICO_DEFAULT_SPI_CSN_PIN   // GP17 - chip select for MCP4912 DAC
#define PIN_CS_RAM  15                         // GP15 - chip select for 23K256 RAM

// ── DAC channel select (from HW7) ─────────────────────────────────────────────
// The MCP4912 has two output channels: A and B
// bit 15 of the 16-bit word tells the chip which channel to write to
#define DAC_CHANNEL_A  0    // bit15 = 0 → channel A (VOUTA, pin 14)
#define DAC_CHANNEL_B  1    // bit15 = 1 → channel B (VOUTB, pin 10)

// ── Signal parameters ─────────────────────────────────────────────────────────
// We store 1000 floats for one full sine wave cycle.
// Reading two bytes at a time with a 1ms delay = 1000 steps * 1ms = 1 second = 1Hz
#define NUM_SAMPLES     1000    // one full sine cycle broken into 1000 steps
#define DELAY_MS        1       // 1ms delay per sample → 1000ms total = 1Hz

// ── 23K256 RAM instruction set ────────────────────────────────────────────────
// These are the 8-bit instructions the RAM chip expects before any read/write
// (from the assignment description: 0b00000010 to write, 0b00000011 to read)
#define RAM_WRITE_CMD   0x02    // 0b00000010 - write instruction
#define RAM_READ_CMD    0x03    // 0b00000011 - read instruction
#define RAM_MODE_CMD    0x01    // 0b00000001 - set mode instruction

// ── 23K256 operation modes ────────────────────────────────────────────────────
// Sequential mode means the address automatically wraps around the entire memory
// space, so we can write/read many bytes in one transaction
#define RAM_SEQ_MODE    0x40    // 0b01000000 - sequential mode

// ── CS helpers (from professor's sample code) ─────────────────────────────────
// CS is "active low": CS=0 means chip is selected/listening, CS=1 means deselected
// The nop instructions are tiny delays to let the pin settle before/after toggling
static inline void cs_select(uint cs_pin) {
    asm volatile("nop \n nop \n nop");
    gpio_put(cs_pin, 0);               // pull CS low = select the chip
    asm volatile("nop \n nop \n nop");
}

static inline void cs_deselect(uint cs_pin) {
    asm volatile("nop \n nop \n nop");
    gpio_put(cs_pin, 1);               // pull CS high = deselect the chip
    asm volatile("nop \n nop \n nop");
}

// ── spi_ram_init ──────────────────────────────────────────────────────────────
// Initializes the 23K256 RAM chip by putting it into sequential mode.
// In sequential mode, the address wraps around the entire memory space,
// so we can write or read many bytes in one transaction without resending
// the instruction and address every time.
//
// To set the mode, we send: mode instruction (0x01), then the mode byte (0x40)
void spi_ram_init() {
    cs_select(PIN_CS_RAM);
    uint8_t cmd[2] = {RAM_MODE_CMD, RAM_SEQ_MODE};  // set sequential mode
    spi_write_blocking(SPI_PORT, cmd, 2);
    cs_deselect(PIN_CS_RAM);
}

// ── spi_ram_write ─────────────────────────────────────────────────────────────
// Writes an array of bytes into the RAM chip starting at the given 16-bit address.
//
// To write into memory:
//   1. Lower the CS pin
//   2. Send the 8-bit write instruction (0x02)
//   3. Send the 16-bit address (high byte first, then low byte)
//   4. Send the data bytes
//   5. Raise the CS pin when done
//
// Parameters:
//   addr = 16-bit starting address in RAM (0 to 32767)
//   data = pointer to the array of bytes to write
//   len  = number of bytes to write
void spi_ram_write(uint16_t addr, uint8_t *data, size_t len) {
    cs_select(PIN_CS_RAM);

    // Send write instruction followed by the 16-bit address (high byte first)
    uint8_t cmd[3];
    cmd[0] = RAM_WRITE_CMD;         // 8-bit write instruction
    cmd[1] = (addr >> 8) & 0xFF;   // high byte of the 16-bit address
    cmd[2] = addr & 0xFF;           // low byte of the 16-bit address
    spi_write_blocking(SPI_PORT, cmd, 3);

    // Now send the actual data bytes
    spi_write_blocking(SPI_PORT, data, len);

    cs_deselect(PIN_CS_RAM);
}

// ── spi_ram_read ──────────────────────────────────────────────────────────────
// Reads an array of bytes from the RAM chip starting at the given 16-bit address.
//
// To read from memory:
//   1. Lower the CS pin
//   2. Send the 8-bit read instruction (0x03)
//   3. Send the 16-bit address (high byte first, then low byte)
//   4. Read as many bytes as you want
//   5. Raise the CS pin when done
//
// Parameters:
//   addr = 16-bit starting address in RAM (0 to 32767)
//   data = pointer to buffer where the read bytes will be stored
//   len  = number of bytes to read
void spi_ram_read(uint16_t addr, uint8_t *data, size_t len) {
    cs_select(PIN_CS_RAM);

    // Send read instruction followed by the 16-bit address (high byte first)
    uint8_t cmd[3];
    cmd[0] = RAM_READ_CMD;          // 8-bit read instruction
    cmd[1] = (addr >> 8) & 0xFF;   // high byte of the 16-bit address
    cmd[2] = addr & 0xFF;           // low byte of the 16-bit address
    spi_write_blocking(SPI_PORT, cmd, 3);

    // Now read the data bytes back into the buffer
    spi_read_blocking(SPI_PORT, 0, data, len);

    cs_deselect(PIN_CS_RAM);
}

// ── writeDAC ──────────────────────────────────────────────────────────────────
// Sends a voltage value to one channel of the MCP4912 over SPI (from HW7).
//
// Parameters:
//   channel = DAC_CHANNEL_A or DAC_CHANNEL_B
//   v       = voltage to output, as a float from 0.0 to 3.3
//
// The MCP4912 expects a 16-bit word split into 2 bytes:
//   Byte 0: [ A/B | BUF | GA | SHDN | D9 | D8 | D7 | D6 ]
//   Byte 1: [ D5  | D4  | D3 | D2   | D1 | D0 | X  | X  ]
void writeDAC(int channel, float v) {
    uint8_t data[2];

    // Start byte 0 with config bits:
    // bit7 (A/B)  = 0 (channel A, OR'd in below)
    // bit6 (BUF)  = 1 (buffered)
    // bit5 (GA)   = 1 (1x gain, output = 0 to 3.3V)
    // bit4 (SHDN) = 1 (active, NOT shutdown)
    data[0] = 0b01111000;

    // OR in the channel select bit at bit position 7
    data[0] = data[0] | ((channel & 0b1) << 7);

    // Convert float voltage (0.0 to 3.3V) to 10-bit integer (0 to 1023)
    uint16_t myV = v / 3.3 * 1023;

    // Put upper 4 bits of myV (bits 9-6) into lower nibble of data[0]
    data[0] = data[0] | ((myV >> 6) & 0b00001111);

    // Put lower 6 bits of myV (bits 5-0) into upper 6 bits of data[1]
    data[1] = (myV << 2) & 0xFF;

    // Send the 2 bytes to the DAC over SPI
    cs_select(PIN_CS_DAC);
    spi_write_blocking(SPI_PORT, data, 2);
    cs_deselect(PIN_CS_DAC);
}

// ── Main ──────────────────────────────────────────────────────────────────────
int main() {
    stdio_init_all();

    // ── SPI init ──────────────────────────────────────────────────────────────
    // MCP4912 and 23K256 both use Mode 0,0: clock idles low, data sampled on rising edge
    spi_init(spi_default, 1000 * 1000);
    spi_set_format(spi_default, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(PICO_DEFAULT_SPI_RX_PIN,  GPIO_FUNC_SPI);  // GP16 - MISO (RAM SO)
    gpio_set_function(PICO_DEFAULT_SPI_SCK_PIN, GPIO_FUNC_SPI);  // GP18 - SCK
    gpio_set_function(PICO_DEFAULT_SPI_TX_PIN,  GPIO_FUNC_SPI);  // GP19 - MOSI

    // DAC CS pin - manually controlled GPIO, starts high (deselected)
    gpio_init(PIN_CS_DAC);
    gpio_set_dir(PIN_CS_DAC, GPIO_OUT);
    gpio_put(PIN_CS_DAC, 1);

    // RAM CS pin - manually controlled GPIO, starts high (deselected)
    gpio_init(PIN_CS_RAM);
    gpio_set_dir(PIN_CS_RAM, GPIO_OUT);
    gpio_put(PIN_CS_RAM, 1);

    // ── Initialize the RAM chip into sequential mode ───────────────────────────
    // This lets us write/read many bytes in one transaction
    spi_ram_init();

    // ── Step 1: Calculate and store 1000 sine wave values into RAM ────────────
    // We calculate one full cycle of a sine wave (0V to 3.3V) broken into
    // 1000 evenly spaced steps. Each float is broken into its 4 raw bytes
    // using a Union (as shown in the hint), then stored in RAM.
    //
    // We also pre-convert each voltage to a 16-bit DAC word using the same
    // bitshifting math from HW7, and store those 2 bytes per sample.
    // That way, in the loop we can read 2 bytes and send them straight to the DAC.

    printf("Storing sine wave into RAM...\n");

    for (int i = 0; i < NUM_SAMPLES; i++) {
        // Calculate the sine voltage for this step
        // sinf() gives -1.0 to 1.0, shift and scale to 0.0 to 3.3V
        float t = (float)i / (float)NUM_SAMPLES;   // 0.0 to just under 1.0
        float voltage = (sinf(2.0f * (float)M_PI * t) + 1.0f) / 2.0f * 3.3f;

        // Convert voltage to a 10-bit DAC value (0 to 1023) using bitshifting from HW7
        uint16_t myV = voltage / 3.3f * 1023;

        // Build the two DAC bytes (same format as writeDAC in HW7):
        //   Byte 0: config bits + upper 4 bits of DAC value
        //   Byte 1: lower 6 bits of DAC value shifted left by 2
        uint8_t dac_bytes[2];
        dac_bytes[0] = 0b01111000;                      // BUF=1, GA=1, SHDN=1, channel A
        dac_bytes[0] = dac_bytes[0] | ((myV >> 6) & 0b00001111);  // upper 4 bits of value
        dac_bytes[1] = (myV << 2) & 0xFF;               // lower 6 bits shifted up

        // Write the 2 DAC bytes for this sample into RAM at address i*2
        // Each sample takes 2 bytes, so sample i lives at address i*2 and i*2+1
        spi_ram_write(i * 2, dac_bytes, 2);
    }

    printf("Done storing. Starting 1Hz sine wave output...\n");

    // ── Step 2: Infinite loop - read from RAM and send to DAC at 1Hz ──────────
    // In the infinite while loop, read two bytes back from RAM at a time
    // and apply them directly to the SPI DAC, with a 1ms delay.
    // 1000 samples * 1ms = 1000ms = 1 second = 1Hz sine wave
    while (1) {
        for (int i = 0; i < NUM_SAMPLES; i++) {
            // Read the 2 pre-built DAC bytes for this sample from RAM
            uint8_t dac_bytes[2];
            spi_ram_read(i * 2, dac_bytes, 2);

            // Send the 2 bytes directly to the DAC - no conversion needed,
            // the bytes are already in the correct MCP4912 format from Step 1
            cs_select(PIN_CS_DAC);
            spi_write_blocking(SPI_PORT, dac_bytes, 2);
            cs_deselect(PIN_CS_DAC);

            // 1ms delay per sample → 1000 samples * 1ms = 1Hz sine wave
            sleep_ms(DELAY_MS);
        }
    }

    return 0;
}