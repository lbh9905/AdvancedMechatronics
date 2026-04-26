// ============================================================
// HW4 - SSD1306 OLED Display with ADC Voltage Reading
// Raspberry Pi Pico 2W (RP2350)
// ============================================================
// WHAT THIS PROGRAM DOES:
//   1. Blinks the onboard LED at 1Hz (heartbeat - proves Pico isn't frozen)
//   2. Blinks a single pixel on the OLED at 1Hz (proves display works)
//   3. Reads ADC0 (GPIO 26) voltage and prints it to the OLED
//   4. Prints the frames-per-second (fps) at the bottom of the screen
//
// OLED DISPLAY LAYOUT (128x32 pixels, 4 rows of text):
//   Row 0 (y=0):  blinking pixel + ADC voltage
//   Row 1 (y=8):  HW4 label
//   Row 2 (y=16): seconds since boot (counts up so you can see it updating)
//   Row 3 (y=24): frames per second
// ============================================================

// --- STANDARD INCLUDES ---
#include <stdio.h>       // for sprintf() - formats text into a char array
#include <string.h>      // for memset() - used inside ssd1306_clear()
#include "pico/stdlib.h" // basic Pico functions: gpio, sleep_ms, etc.

// --- HARDWARE INCLUDES ---
#include "hardware/i2c.h"    // needed to talk to the OLED over I2C
#include "hardware/adc.h"    // needed to read analog voltage on ADC0
#include "pico/cyw43_arch.h" // needed to control the onboard LED on Pico W/2W
                             // the LED is wired through the CYW43 wifi chip,
                             // NOT directly to a GPIO pin like on the regular Pico

// --- OUR OWN FILES ---
#include "ssd1306.h"  // functions: ssd1306_setup, ssd1306_clear,
                      //            ssd1306_drawPixel, ssd1306_update
#include "font.h"     // ASCII[][] lookup table for drawing characters
                      // drawChar() and drawString() are defined below


// ============================================================
// PIN DEFINITIONS
// ============================================================
// NOTE: On the Pico 2W, the LED is controlled through the CYW43 wifi chip.
// We use cyw43_arch functions instead of gpio_put().

#define I2C_SDA    0    // GPIO 0 = SDA (data line for I2C)
#define I2C_SCL    1    // GPIO 1 = SCL (clock line for I2C)
#define ADC_PIN    26   // GPIO 26 = ADC0 (analog input)


// ============================================================
// FUNCTION: drawChar
// PURPOSE:  Draws a single character on the OLED at position (x, y)
//
// HOW IT WORKS:
//   - font.h stores each character as 5 bytes (5 columns)
//   - each byte has 8 bits, one bit per row (pixel)
//   - we loop through every column (0-4) and every row (0-7)
//   - we check if that bit is 1 (pixel on) or 0 (pixel off)
//   - we call ssd1306_drawPixel() for each pixel
//
// PARAMETERS:
//   x - horizontal position of the LEFT edge of the character
//   y - vertical position of the TOP edge of the character
//   c - the character to draw (e.g. 'A', '3', ':')
// ============================================================
void drawChar(int x, int y, char c) {

    // The ASCII table in font.h starts at the space character (0x20 = 32)
    // So to get the right row in the table, subtract 0x20 from the character
    // Example: 'A' is ASCII 65 (0x41), so index = 0x41 - 0x20 = 33
    int charIndex = c - 0x20;

    // Loop through each of the 5 columns of the character
    for (int col = 0; col < 5; col++) {

        // Get the byte for this column from the font table
        // This byte has 8 bits, each bit = one pixel in that column
        char columnData = ASCII[charIndex][col];

        // Loop through each of the 8 rows (bits) in this column
        for (int row = 0; row < 8; row++) {

            // BIT EXTRACTION TRICK:
            // (columnData >> row) shifts the byte right by 'row' positions
            // & 1 masks off everything except the last bit
            // Result is either 1 (pixel on) or 0 (pixel off)
            int pixelOn = (columnData >> row) & 1;

            // Draw the pixel at the correct screen position
            // x + col  = moves right for each column of the character
            // y + row  = moves down for each row of the character
            ssd1306_drawPixel(x + col, y + row, pixelOn);
        }
    }
}


// ============================================================
// FUNCTION: drawString
// PURPOSE:  Draws a full string (array of chars) on the OLED
//
// HOW IT WORKS:
//   - loops through each character in the string
//   - stops when it hits the null terminator '\0' (end of string)
//   - calls drawChar() for each character
//   - each character is 5 pixels wide, so we shift x by 5 each time
//
// PARAMETERS:
//   x   - horizontal start position
//   y   - vertical start position
//   str - pointer to the character array (string)
// ============================================================
void drawString(int x, int y, char* str) {

    int i = 0; // index into the string

    // Keep going until we hit the null terminator (end of string)
    // '\0' is the null character, value = 0, marks end of a C string
    while (str[i] != '\0') {

        // Draw the character at x offset by (i * 5) pixels
        // Each character is 5 pixels wide, so multiply index by 5
        drawChar(x + i * 5, y, str[i]);

        i++; // move to the next character
    }
}


// ============================================================
// MAIN FUNCTION
// Everything starts here
// ============================================================
int main() {

    // --- INITIALIZE STANDARD I/O ---
    // This lets you use printf() to send debug messages over USB serial
    stdio_init_all();


    // --------------------------------------------------------
    // SET UP THE ONBOARD LED (Pico 2W version)
    // --------------------------------------------------------
    // On the Pico 2W, the LED is wired to the CYW43 wifi chip, not a GPIO.
    // cyw43_arch_init() starts up the wifi chip so we can use the LED.
    // "_none" means: start the chip but don't connect to any wifi network.
    if (cyw43_arch_init()) {
        // If init fails, something is very wrong - just hang here
        while (true) {}
    }


    // --------------------------------------------------------
    // SET UP I2C FOR THE OLED DISPLAY
    // --------------------------------------------------------
    // Initialize I2C at 400,000 Hz (400 kHz = "fast mode")
    // i2c_default is i2c0, which uses GPIO 0 (SDA) and GPIO 1 (SCL)
    i2c_init(i2c_default, 400 * 1000);

    // Tell the Pico that GPIO 0 and 1 are I2C pins (not regular GPIO)
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);

    // Enable internal pull-up resistors on SDA and SCL
    // I2C needs pull-ups so the lines default to HIGH when idle
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // Run the ssd1306 startup sequence
    // This sends a series of commands to wake up and configure the display
    ssd1306_setup();


    // --------------------------------------------------------
    // SET UP ADC (Analog to Digital Converter)
    // --------------------------------------------------------
    // Initialize the ADC hardware block
    adc_init();

    // Set GPIO 26 as an ADC input (disables digital functions on that pin)
    adc_gpio_init(ADC_PIN);

    // Select ADC channel 0 (GPIO 26 = channel 0)
    // Pico has channels 0,1,2 on GPIO 26,27,28 and channel 3 is internal temp
    adc_select_input(0);


    // --------------------------------------------------------
    // VARIABLES FOR TIMING AND DISPLAY
    // --------------------------------------------------------
    // These track the LED and pixel blink state (true = on, false = off)
    bool ledState = false;
    bool pixelState = false;

    // lastBlinkTime tracks when we last toggled the LED/pixel
    // to_us_since_boot() returns microseconds since the Pico powered on
    unsigned int lastBlinkTime = to_us_since_boot(get_absolute_time());

    // These variables are for calculating frames per second
    unsigned int frameCount = 0;
    unsigned int lastFpsTime = to_us_since_boot(get_absolute_time());
    float fps = 0.0f;

    // Buffer to hold formatted text strings before drawing them
    char message[50];


    // ============================================================
    // MAIN LOOP - runs forever
    // ============================================================
    while (true) {

        // --------------------------------------------------------
        // STEP 1: GET CURRENT TIME
        // --------------------------------------------------------
        // Read microseconds since boot into variable 't'
        unsigned int t = to_us_since_boot(get_absolute_time());


        // --------------------------------------------------------
        // STEP 2: BLINK LED AND PIXEL AT 1Hz
        // --------------------------------------------------------
        // 1 Hz = toggle every 500,000 microseconds (0.5 seconds)
        if (t - lastBlinkTime >= 500000) {

            // Toggle the LED state (flip true <-> false)
            ledState = !ledState;

            // On Pico 2W use cyw43 function instead of gpio_put()
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, ledState);

            // Toggle the pixel blink state too
            pixelState = !pixelState;

            // Reset the blink timer
            lastBlinkTime = t;
        }


        // --------------------------------------------------------
        // STEP 3: READ ADC VOLTAGE
        // --------------------------------------------------------
        // adc_read() returns 0-4095 (12-bit)
        // 0 = 0V, 4095 = 3.3V
        uint16_t rawADC = adc_read();

        // Convert raw value to voltage
        float voltage = (rawADC / 4095.0f) * 3.3f;


        // --------------------------------------------------------
        // STEP 4: CALCULATE FRAMES PER SECOND
        // --------------------------------------------------------
        frameCount++;

        // Every 1 second (1,000,000 us), calculate fps
        if (t - lastFpsTime >= 1000000) {
            fps = (float)frameCount;
            frameCount = 0;
            lastFpsTime = t;
        }


        // --------------------------------------------------------
        // STEP 5: DRAW EVERYTHING TO THE DISPLAY
        // --------------------------------------------------------
        // The screen is 128x32 pixels, characters are 5px wide x 8px tall
        // That gives us 25 characters across and 4 rows of text:
        //   Row 0 = y=0
        //   Row 1 = y=8
        //   Row 2 = y=16
        //   Row 3 = y=24

        // Clear the display buffer first (sets all pixels to off)
        // Nothing changes on screen until ssd1306_update() is called
        ssd1306_clear();

        // --- ROW 0 (y=0): blinking pixel + ADC voltage ---
        // The pixel at (0,0) blinks to show the loop is running
        ssd1306_drawPixel(0, 0, pixelState);
        // Print ADC voltage starting at x=6 to leave room for the pixel
        sprintf(message, "ADC: %.2fV", voltage);
        drawString(6, 0, message);

        // --- ROW 1 (y=8): static label to show display is working ---
        // This fills up the second row with a fixed message
        sprintf(message, "HW4 OLED TEST");
        drawString(0, 8, message);

        // --- ROW 2 (y=16): seconds since boot ---
        // t is in microseconds, dividing by 1,000,000 gives seconds
        // This counts up every second so you can see the display is live
        sprintf(message, "Uptime: %us", t / 1000000);
        drawString(0, 16, message);

        // --- ROW 3 (y=24): frames per second ---
        // This tells you how fast the display is updating
        sprintf(message, "FPS: %.1f", fps);
        drawString(0, 24, message);

        // --- Push the buffer to the actual OLED screen ---
        // Nothing shows up until you call ssd1306_update()!
        ssd1306_update();

    } // end while(true)

    return 0; // never actually reached, but good practice
}