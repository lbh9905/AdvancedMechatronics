// ============================================================
// HW5 - MPU6050 IMU + SSD1306 OLED Display
// Raspberry Pi Pico 2W (RP2350)
// ============================================================
// WHAT THIS PROGRAM DOES:
//   1. Initializes I2C and verifies the MPU6050 is connected
//      via WHO_AM_I register check
//   2. If WHO_AM_I fails, blinks onboard LED forever (error trap)
//   3. Configures MPU6050:
//        - Wakes chip (PWR_MGMT_1)
//        - Accelerometer: ±2g sensitivity
//        - Gyroscope:     ±2000 dps sensitivity
//   4. Reads all IMU data at 100Hz via 14-byte burst read
//   5. Prints accel (g), gyro (dps), and temp (°F) to USB serial
//   6. Draws X and Y acceleration vectors as lines on the OLED
//      from the center of the screen, proportional to magnitude
// ============================================================

// --- STANDARD INCLUDES ---
#include <stdio.h>       // printf(), sprintf()
#include <string.h>      // memset()
#include <math.h>        // not strictly needed but useful for scaling
#include <stdlib.h>      // abs()
#include "pico/stdlib.h" // gpio, sleep_ms, timing, etc.

// --- HARDWARE INCLUDES ---
#include "hardware/i2c.h"    // i2c_write_blocking, i2c_read_blocking
#include "pico/cyw43_arch.h" // onboard LED control on Pico 2W

// --- OUR OWN FILES ---
#include "ssd1306.h"  // ssd1306_setup, ssd1306_clear, ssd1306_drawPixel, ssd1306_update
#include "font.h"     // ASCII font table for drawChar/drawString


// ============================================================
// PIN DEFINITIONS
// ============================================================
#define I2C_SDA    0    // GPIO 0 = SDA
#define I2C_SCL    1    // GPIO 1 = SCL


// ============================================================
// MPU6050 I2C ADDRESS
// ============================================================
// AD0 pin is LOW by default on the breakout board → address = 0x68
// If you have an MPU-6050M (the variant mentioned in the assignment),
// it might respond at 0x68 OR 0x98 — we check WHO_AM_I to confirm
#define MPU_ADDR   0x68


// ============================================================
// MPU6050 REGISTER DEFINITIONS
// ============================================================
// Config registers
#define CONFIG        0x1A
#define GYRO_CONFIG   0x1B
#define ACCEL_CONFIG  0x1C
#define PWR_MGMT_1    0x6B
#define PWR_MGMT_2    0x6C

// Sensor data registers (each measurement = 2 bytes, HIGH then LOW)
#define ACCEL_XOUT_H  0x3B
#define ACCEL_XOUT_L  0x3C
#define ACCEL_YOUT_H  0x3D
#define ACCEL_YOUT_L  0x3E
#define ACCEL_ZOUT_H  0x3F
#define ACCEL_ZOUT_L  0x40
#define TEMP_OUT_H    0x41
#define TEMP_OUT_L    0x42
#define GYRO_XOUT_H   0x43
#define GYRO_XOUT_L   0x44
#define GYRO_YOUT_H   0x45
#define GYRO_YOUT_L   0x46
#define GYRO_ZOUT_H   0x47
#define GYRO_ZOUT_L   0x48
#define WHO_AM_I      0x75


// ============================================================
// SCALING CONSTANTS
// ============================================================
// These come from the MPU6050 datasheet for the chosen sensitivity ranges
#define ACCEL_SCALE   0.000061f   // ±2g range    → multiply raw to get g
#define GYRO_SCALE    0.007630f   // ±2000dps range → multiply raw to get °/s


// ============================================================
// OLED DISPLAY CONSTANTS
// ============================================================
#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  32
#define CENTER_X       (SCREEN_WIDTH  / 2)   // = 64
#define CENTER_Y       (SCREEN_HEIGHT / 2)   // = 16

// How many pixels = 1g of acceleration for the vector lines
// At ±2g, max raw value is ~32767, scaled = ~2.0g
// We want 2g to reach roughly the edge of the screen (16 pixels from center)
// So scale factor = 16 pixels / 2.0g = 8 pixels per g
#define VECTOR_SCALE   14.0f


// ============================================================
// FUNCTION: drawChar
// (same as HW4 - draws one character from font.h at position x,y)
// ============================================================
void drawChar(int x, int y, char c) {
    int charIndex = c - 0x20;
    for (int col = 0; col < 5; col++) {
        char columnData = ASCII[charIndex][col];
        for (int row = 0; row < 8; row++) {
            int pixelOn = (columnData >> row) & 1;
            ssd1306_drawPixel(x + col, y + row, pixelOn);
        }
    }
}


// ============================================================
// FUNCTION: drawString
// (same as HW4 - draws a full string at position x,y)
// ============================================================
void drawString(int x, int y, char* str) {
    int i = 0;
    while (str[i] != '\0') {
        drawChar(x + i * 5, y, str[i]);
        i++;
    }
}


// ============================================================
// FUNCTION: mpu_write_register
// PURPOSE:  Writes one byte to a specific MPU6050 register
//
// HOW IT WORKS:
//   I2C writes need: [register address, data byte]
//   We put both in a 2-byte buffer and call i2c_write_blocking()
//
// PARAMETERS:
//   reg  - the register address to write to (e.g. PWR_MGMT_1)
//   data - the byte value to write (e.g. 0x00 to wake chip)
// ============================================================
void mpu_write_register(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    i2c_write_blocking(i2c_default, MPU_ADDR, buf, 2, false);
}


// ============================================================
// FUNCTION: mpu_read_register
// PURPOSE:  Reads one byte from a specific MPU6050 register
//
// HOW IT WORKS:
//   First send the register address we want to read from (write with nostop=true)
//   Then read back 1 byte (the register value)
//
// PARAMETERS:
//   reg - the register address to read from
//
// RETURNS:
//   The byte value stored in that register
// ============================================================
uint8_t mpu_read_register(uint8_t reg) {
    uint8_t result;
    // Send the register address, keep bus active (nostop = true)
    i2c_write_blocking(i2c_default, MPU_ADDR, &reg, 1, true);
    // Read back 1 byte
    i2c_read_blocking(i2c_default, MPU_ADDR, &result, 1, false);
    return result;
}


// ============================================================
// FUNCTION: mpu_init
// PURPOSE:  Wakes up the MPU6050 and configures accel + gyro
//
// REGISTERS CHANGED:
//   PWR_MGMT_1   → 0x00  wakes the chip (clears sleep bit)
//   ACCEL_CONFIG → 0x00  sets accelerometer to ±2g
//   GYRO_CONFIG  → 0x18  sets gyroscope to ±2000 dps
//
// ACCEL_CONFIG bits [4:3] = FS_SEL:
//   00 = ±2g, 01 = ±4g, 10 = ±8g, 11 = ±16g
//   0x00 = 0b00000000 → ±2g
//
// GYRO_CONFIG bits [4:3] = FS_SEL:
//   00 = ±250dps, 01 = ±500dps, 10 = ±1000dps, 11 = ±2000dps
//   0x18 = 0b00011000 → ±2000dps
// ============================================================
void mpu_init() {
    mpu_write_register(PWR_MGMT_1,  0x00);  // wake up chip
    mpu_write_register(ACCEL_CONFIG, 0x00); // ±2g
    mpu_write_register(GYRO_CONFIG,  0x18); // ±2000 dps
}


// ============================================================
// STRUCT: IMUData
// PURPOSE: Holds all scaled IMU readings in one place
// ============================================================
typedef struct {
    float accel_x, accel_y, accel_z;  // in g
    float gyro_x,  gyro_y,  gyro_z;   // in degrees/second
    float temp_f;                      // in degrees Fahrenheit
} IMUData;


// ============================================================
// FUNCTION: mpu_read_all
// PURPOSE:  Reads all 14 bytes of sensor data in one burst read,
//           recombines into signed 16-bit integers, and scales
//
// HOW IT WORKS:
//   The 14 data bytes starting at ACCEL_XOUT_H are laid out as:
//     [0-1]  = Accel X (high byte, low byte)
//     [2-3]  = Accel Y
//     [4-5]  = Accel Z
//     [6-7]  = Temperature
//     [8-9]  = Gyro X
//     [10-11]= Gyro Y
//     [12-13]= Gyro Z
//
//   To recombine: value = (high_byte << 8) | low_byte
//   This gives a uint16, cast to int16_t to get the signed value
//
// RETURNS:
//   IMUData struct with all scaled values
// ============================================================
IMUData mpu_read_all() {
    uint8_t raw[14];
    uint8_t reg = ACCEL_XOUT_H;

    // Tell the chip we want to start reading from ACCEL_XOUT_H
    // nostop = true keeps the bus active so we can immediately read
    i2c_write_blocking(i2c_default, MPU_ADDR, &reg, 1, true);

    // Burst read all 14 bytes in one go
    i2c_read_blocking(i2c_default, MPU_ADDR, raw, 14, false);

    // --- Recombine high + low bytes into signed 16-bit integers ---
    // (high << 8) | low shifts the high byte into the upper 8 bits
    // then ORs in the low byte to fill the lower 8 bits
    // cast to int16_t so the value is properly signed (can be negative)
    int16_t raw_ax = (int16_t)((raw[0]  << 8) | raw[1]);
    int16_t raw_ay = (int16_t)((raw[2]  << 8) | raw[3]);
    int16_t raw_az = (int16_t)((raw[4]  << 8) | raw[5]);
    int16_t raw_t  = (int16_t)((raw[6]  << 8) | raw[7]);
    int16_t raw_gx = (int16_t)((raw[8]  << 8) | raw[9]);
    int16_t raw_gy = (int16_t)((raw[10] << 8) | raw[11]);
    int16_t raw_gz = (int16_t)((raw[12] << 8) | raw[13]);

    // --- Scale to real units ---
    IMUData d;
    d.accel_x = raw_ax * ACCEL_SCALE;   // g
    d.accel_y = raw_ay * ACCEL_SCALE;   // g
    d.accel_z = raw_az * ACCEL_SCALE;   // g
    d.gyro_x  = raw_gx * GYRO_SCALE;   // degrees/sec
    d.gyro_y  = raw_gy * GYRO_SCALE;   // degrees/sec
    d.gyro_z  = raw_gz * GYRO_SCALE;   // degrees/sec

    // Temperature: formula from datasheet gives °C, we convert to °F
    float temp_c = (raw_t / 340.0f) + 36.53f;
    d.temp_f = temp_c * 9.0f / 5.0f + 32.0f;

    return d;
}


// ============================================================
// FUNCTION: draw_line
// PURPOSE:  Draws a line on the OLED from (x0,y0) to (x1,y1)
//           using Bresenham's line algorithm
//
// WHY WE NEED THIS:
//   ssd1306_drawPixel() only draws one pixel at a time.
//   To draw a vector line we need to light up all the pixels
//   between the start and end points.
//
// HOW BRESENHAM'S WORKS:
//   It steps along the longer axis one pixel at a time and
//   decides whether to step on the shorter axis based on
//   accumulated error. Integer math only, very efficient.
// ============================================================
void draw_line(int x0, int y0, int x1, int y1) {
    int dx =  abs(x1 - x0);
    int dy = -abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;  // step direction on x
    int sy = (y0 < y1) ? 1 : -1;  // step direction on y
    int err = dx + dy;

    while (true) {
        // Only draw if pixel is within screen bounds
        if (x0 >= 0 && x0 < SCREEN_WIDTH && y0 >= 0 && y0 < SCREEN_HEIGHT) {
            ssd1306_drawPixel(x0, y0, 1);
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}



// ============================================================
// FUNCTION: draw_vectors
// PURPOSE:  Draws X and Y acceleration vectors on the OLED
//           as lines from the center of the screen
//
// HOW IT WORKS:
//   - Center of the 128x32 screen is (64, 16)
//   - X acceleration moves the endpoint left/right
//   - Y acceleration moves the endpoint up/down
//   - VECTOR_SCALE converts g values to pixels
//   - Both X and Y components are combined into ONE line
//     from center to (cx + ax_pixels, cy + ay_pixels)
//     so the line points in the direction gravity is pulling
//
// PARAMETERS:
//   ax - X acceleration in g
//   ay - Y acceleration in g
// ============================================================
void draw_vectors(float ax, float ay) {
    // Calculate endpoint of the vector line
    // We clamp to screen bounds just in case
    int end_x = CENTER_X - (int)(ax * VECTOR_SCALE);
    int end_y = CENTER_Y - (int)(ay * VECTOR_SCALE);

    // Clamp to screen
    if (end_x < 0)              end_x = 0;
    if (end_x >= SCREEN_WIDTH)  end_x = SCREEN_WIDTH  - 1;
    if (end_y < 0)              end_y = 0;
    if (end_y >= SCREEN_HEIGHT) end_y = SCREEN_HEIGHT - 1;

    // Draw a small crosshair at the center so you can see origin
    ssd1306_drawPixel(CENTER_X,     CENTER_Y,     1);
    ssd1306_drawPixel(CENTER_X + 1, CENTER_Y,     1);
    ssd1306_drawPixel(CENTER_X - 1, CENTER_Y,     1);
    ssd1306_drawPixel(CENTER_X,     CENTER_Y + 1, 1);
    ssd1306_drawPixel(CENTER_X,     CENTER_Y - 1, 1);

    // Draw the vector line from center to endpoint
    draw_line(CENTER_X, CENTER_Y, end_x, end_y);
    
}


// ============================================================
// MAIN FUNCTION
// ============================================================
int main() {

    // --- INITIALIZE STANDARD I/O ---
    stdio_init_all();

    // Small delay so USB serial has time to connect before we
    // start printing (otherwise you miss the first few prints)
    sleep_ms(2000);


    // --------------------------------------------------------
    // SET UP ONBOARD LED (Pico 2W uses CYW43 chip for LED)
    // --------------------------------------------------------
    if (cyw43_arch_init()) {
        while (true) {}  // if wifi chip fails to init, hang forever
    }


    // --------------------------------------------------------
    // SET UP I2C
    // --------------------------------------------------------
    // Both the MPU6050 and SSD1306 share the same I2C bus
    // MPU6050 address = 0x68, SSD1306 address = 0x3C → no conflict
    i2c_init(i2c_default, 400 * 1000);  // 400kHz fast mode
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);


    // --------------------------------------------------------
    // SET UP OLED
    // --------------------------------------------------------
    ssd1306_setup();


    // --------------------------------------------------------
    // CHECK WHO_AM_I REGISTER
    // --------------------------------------------------------
    // WHO_AM_I should return 0x68 (or 0x98 for some MPU-6050M variants)
    // If we get something else, the chip isn't responding correctly
    uint8_t whoami = mpu_read_register(WHO_AM_I);
    if (whoami != 0x68 && whoami != 0x98) {
        // Something is wrong - blink LED forever so user knows to power cycle
        while (true) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            sleep_ms(500);
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            sleep_ms(500);
        }
    }


    // --------------------------------------------------------
    // INITIALIZE MPU6050
    // --------------------------------------------------------
    mpu_init();


    // --------------------------------------------------------
    // TIMING VARIABLES
    // --------------------------------------------------------
    // We want to read at exactly 100Hz = once every 10,000 microseconds
    char message[50];


    // ============================================================
    // MAIN LOOP - runs at 100Hz
    // ============================================================
    while (true) {

        // Record the time at the START of this loop iteration
        // so we can sleep the exact right amount at the end
        unsigned int t_start = to_us_since_boot(get_absolute_time());


        // --------------------------------------------------------
        // READ IMU DATA
        // --------------------------------------------------------
        IMUData imu = mpu_read_all();


        // --------------------------------------------------------
        // PRINT TO SERIAL TERMINAL
        // --------------------------------------------------------
        // All 7 values printed in one line, tab separated for easy reading
        printf("Accel(g): X=%.3f Y=%.3f Z=%.3f | "
               "Gyro(dps): X=%.2f Y=%.2f Z=%.2f | "
               "Temp: %.1f F\n",
               imu.accel_x, imu.accel_y, imu.accel_z,
               imu.gyro_x,  imu.gyro_y,  imu.gyro_z,
               imu.temp_f);


        // --------------------------------------------------------
        // UPDATE OLED DISPLAY
        // --------------------------------------------------------
        ssd1306_clear();

        // Draw the acceleration vector lines
        draw_vectors(imu.accel_x, imu.accel_y);

        // print a small accel readout at top of screen
        sprintf(message, "X:%.2f", imu.accel_x);
        drawString(0, 0, message);
        sprintf(message, "Y:%.2f", imu.accel_y);
        drawString(0, 8, message);

        ssd1306_update();


        // --------------------------------------------------------
        // SLEEP TO MAINTAIN 100Hz
        // --------------------------------------------------------
        // 100Hz = 10,000 microseconds per loop
        // Subtract how long this iteration already took
        unsigned int t_end = to_us_since_boot(get_absolute_time());
        unsigned int elapsed = t_end - t_start;

        if (elapsed < 10000) {
            sleep_us(10000 - elapsed);
        }
        // If elapsed >= 10000, we're already behind — skip the sleep

    } // end while(true)

    return 0;
}