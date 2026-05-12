// ============================================================
// HW6 - USB HID Mouse with MPU6050 + Mode Toggle
// Raspberry Pi Pico 2W (RP2350)
// ============================================================
// MODES:
//   Regular mode  (LED OFF): IMU controls mouse position
//                             X accel → mouse X, Y accel → mouse Y
//                             4 speed levels based on acceleration
//   Remote mode   (LED ON):  Mouse moves in a slow circle
//                             (simulates "I'm still here" for remote work)
//
// HARDWARE:
//   Button  → GP19 (internal pull-up, active LOW)
//   LED     → GP14 (HIGH = on)
//   I2C SDA → GP0
//   I2C SCL → GP1
//   MPU6050 → 0x68
//   OLED    → 0x3C (shared I2C bus, used for debug)
// ============================================================

// --- STANDARD INCLUDES ---
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// --- PICO / TINYUSB INCLUDES ---
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

// --- HARDWARE INCLUDES ---
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"

// --- OUR OWN FILES ---
#include "ssd1306.h"
#include "font.h"

// ============================================================
// PIN DEFINITIONS
// ============================================================
#define I2C_SDA     0    // GPIO 0 = SDA (shared by MPU6050 + OLED)
#define I2C_SCL     1    // GPIO 1 = SCL
#define BUTTON_PIN  19   // Mode toggle button (active LOW, internal pull-up)
#define LED_PIN     14   // Mode indicator LED (HIGH = remote working mode)

// ============================================================
// MPU6050 DEFINITIONS
// ============================================================
#define MPU_ADDR      0x68
#define PWR_MGMT_1    0x6B
#define ACCEL_CONFIG  0x1C
#define GYRO_CONFIG   0x1B
#define ACCEL_XOUT_H  0x3B
#define WHO_AM_I      0x75
#define ACCEL_SCALE   0.000061f   // ±2g range

// ============================================================
// OLED SCREEN CONSTANTS (for debug display)
// ============================================================
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 32

// ============================================================
// BLINK PATTERN (onboard LED via board_led_write)
//   250ms  = not mounted
//   1000ms = mounted
//   2500ms = suspended
// ============================================================
enum {
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED     = 1000,
  BLINK_SUSPENDED   = 2500,
};
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// ============================================================
// MODE TRACKING
// ============================================================
// false = regular mode (IMU controls mouse)
// true  = remote working mode (slow circle)
static bool remote_mode = false;

// ============================================================
// MPU6050 STRUCT + FUNCTIONS
// (ported from HW5)
// ============================================================
typedef struct {
    float accel_x, accel_y, accel_z;
} IMUData;

void mpu_write_register(uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    i2c_write_blocking(i2c_default, MPU_ADDR, buf, 2, false);
}

uint8_t mpu_read_register(uint8_t reg) {
    uint8_t result;
    i2c_write_blocking(i2c_default, MPU_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDR, &result, 1, false);
    return result;
}

void mpu_init() {
    mpu_write_register(PWR_MGMT_1,   0x00);  // wake up chip
    mpu_write_register(ACCEL_CONFIG, 0x00);  // ±2g
    mpu_write_register(GYRO_CONFIG,  0x18);  // ±2000 dps
}

IMUData mpu_read_accel() {
    uint8_t raw[6];
    uint8_t reg = ACCEL_XOUT_H;
    i2c_write_blocking(i2c_default, MPU_ADDR, &reg, 1, true);
    i2c_read_blocking(i2c_default, MPU_ADDR, raw, 6, false);

    int16_t raw_ax = (int16_t)((raw[0] << 8) | raw[1]);
    int16_t raw_ay = (int16_t)((raw[2] << 8) | raw[3]);
    int16_t raw_az = (int16_t)((raw[4] << 8) | raw[5]);

    IMUData d;
    d.accel_x = raw_ax * ACCEL_SCALE;
    d.accel_y = raw_ay * ACCEL_SCALE;
    d.accel_z = raw_az * ACCEL_SCALE;
    return d;
}

// ============================================================
// OLED HELPER FUNCTIONS
// (ported from HW5)
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

void drawString(int x, int y, char* str) {
    int i = 0;
    while (str[i] != '\0') {
        drawChar(x + i * 5, y, str[i]);
        i++;
    }
}

// ============================================================
// ACCELERATION → MOUSE SPEED (4 levels)
// ============================================================
// Maps a float acceleration value (in g) to a mouse delta (-5 to +5)
// Dead zone below 0.1g so the cursor doesn't drift when chip is still
// 4 speed levels: slow, medium, fast, very fast
// ============================================================
int8_t accel_to_delta(float accel) {
    float a = accel;
    float abs_a = (a < 0) ? -a : a;
    int8_t sign = (a < 0) ? -1 : 1;

    if (abs_a < 0.15f) return 0;        // dead zone — no movement
    else if (abs_a < 0.4f) return sign * 2;   // slow
    else if (abs_a < 0.7f) return sign * 4;   // medium
    else if (abs_a < 1.0f) return sign * 7;   // fast
    else                   return sign * 10;  // very fast (nearly vertical tilt)
}

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void led_blinking_task(void);
void hid_task(void);
void button_task(void);

// ============================================================
// MAIN
// ============================================================
int main(void) {
    // --- Init board + TinyUSB ---
    board_init();
    tud_init(BOARD_TUD_RHPORT);
    if (board_init_after_tusb) {
        board_init_after_tusb();
    }

    // --- Init I2C (shared bus for MPU6050 + OLED) ---
    i2c_init(i2c_default, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);

    // --- Init OLED ---
    ssd1306_setup();

    // --- Check MPU6050 WHO_AM_I ---
    uint8_t whoami = mpu_read_register(WHO_AM_I);
    if (whoami != 0x68 && whoami != 0x98) {
        // MPU6050 not found — show error on OLED and halt
        ssd1306_clear();
        drawString(0, 0, "MPU ERR!");
        ssd1306_update();
        while (true) { sleep_ms(500); }
    }

    // --- Init MPU6050 ---
    mpu_init();

    // --- Init button on GP19 with internal pull-up ---
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);  // button reads LOW when pressed

    // --- Init mode indicator LED on GP14 ---
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);  // start with LED off (regular mode)

    // --- Main loop ---
    while (1) {
        tud_task();           // TinyUSB device task
        led_blinking_task();  // onboard LED blink (USB status)
        button_task();        // check for mode toggle
        hid_task();           // send mouse report
    }
}

// ============================================================
// BUTTON TASK
// Polls GP19 every 10ms with simple debounce.
// Toggles remote_mode and updates LED on GP14.
// ============================================================
void button_task(void) {
    static uint32_t last_press_ms = 0;
    const uint32_t debounce_ms = 200;  // ignore bounces within 200ms

    // Button is active LOW (pressed = 0) due to pull-up
    if (!gpio_get(BUTTON_PIN)) {
        uint32_t now = board_millis();
        if (now - last_press_ms > debounce_ms) {
            last_press_ms = now;
            remote_mode = !remote_mode;               // toggle mode
            gpio_put(LED_PIN, remote_mode ? 1 : 0);  // LED on = remote mode
        }
    }
}

// ============================================================
// SEND HID REPORT
// Only sends REPORT_ID_MOUSE.
// Regular mode: IMU accel → delta X/Y
// Remote mode:  slow circle using sin/cos counter
// ============================================================
static void send_hid_report(uint8_t report_id, uint32_t btn) {
    if (!tud_hid_ready()) return;

    if (report_id == REPORT_ID_MOUSE) {
        int8_t delta_x = 0;
        int8_t delta_y = 0;

        if (!remote_mode) {
            // ---- REGULAR MODE: IMU controls mouse ----
            IMUData imu = mpu_read_accel();
            delta_x = accel_to_delta(-imu.accel_x);
            delta_y = accel_to_delta(-imu.accel_y);

            // Debug: show accel values on OLED
            char msg[20];
            ssd1306_clear();
            drawString(0, 0, "IMU MODE");
            sprintf(msg, "X:%.2f", imu.accel_x);
            drawString(0, 8, msg);
            sprintf(msg, "Y:%.2f", imu.accel_y);
            drawString(0, 16, msg);
            ssd1306_update();

        } else {
            // ---- REMOTE WORKING MODE: slow circle ----
            // We use a counter that increments each report (every 10ms)
            // Full circle = 360 steps → takes 3.6 seconds per revolution
            static int circle_step = 0;
            const int CIRCLE_STEPS = 360;
            const float CIRCLE_RADIUS = 3.0f;  // pixels per step (small = slow)

            float angle = (2.0f * 3.14159f * circle_step) / CIRCLE_STEPS;
            delta_x = (int8_t)(CIRCLE_RADIUS * cosf(angle));
            delta_y = (int8_t)(CIRCLE_RADIUS * sinf(angle));

            circle_step = (circle_step + 1) % CIRCLE_STEPS;

            // Debug: show mode on OLED
            ssd1306_clear();
            drawString(0, 0, "REMOTE");
            drawString(0, 8, "MODE ON");
            ssd1306_update();
        }

        // Send mouse report: no buttons, delta X/Y, no scroll, no pan
        tud_hid_mouse_report(REPORT_ID_MOUSE, 0x00, delta_x, delta_y, 0, 0);
    }
}

// ============================================================
// HID TASK
// Sends mouse report every 10ms.
// ============================================================
void hid_task(void) {
    const uint32_t interval_ms = 10;
    static uint32_t start_ms = 0;

    if (board_millis() - start_ms < interval_ms) return;
    start_ms += interval_ms;

    uint32_t const btn = board_button_read();

    if (tud_suspended() && btn) {
        tud_remote_wakeup();
    } else {
        send_hid_report(REPORT_ID_MOUSE, btn);
    }
}

// ============================================================
// TUD CALLBACKS
// ============================================================
void tud_mount_cb(void)    { blink_interval_ms = BLINK_MOUNTED; }
void tud_umount_cb(void)   { blink_interval_ms = BLINK_NOT_MOUNTED; }
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    blink_interval_ms = BLINK_SUSPENDED;
}
void tud_resume_cb(void) {
    blink_interval_ms = tud_mounted() ? BLINK_MOUNTED : BLINK_NOT_MOUNTED;
}

// Called after each HID report is sent — for composite reports
// we only have mouse now so nothing extra needed
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void) instance;
    (void) report;
    (void) len;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer;   (void) reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void) instance; (void) report_id; (void) report_type;
    (void) buffer;   (void) bufsize;
}

// ============================================================
// BLINKING TASK (onboard LED = USB status indicator)
// ============================================================
void led_blinking_task(void) {
    static uint32_t start_ms = 0;
    static bool led_state = false;

    if (!blink_interval_ms) return;
    if (board_millis() - start_ms < blink_interval_ms) return;
    start_ms += blink_interval_ms;

    board_led_write(led_state);
    led_state = !led_state;
}