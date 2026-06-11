#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "pico/time.h"
// ── I2C ─────────────────────────────────────────────────────────
#define I2C_PORT     i2c0
#define SDA_PIN      4
#define SCL_PIN      5

// ── AS5600 ──────────────────────────────────────────────────────
#define AS5600_ADDR  0x36
#define REG_ANGLE_H  0x0E

// ── INA219 ──────────────────────────────────────────────────────
#define INA219_ADDR     0x40
#define REG_CONFIG      0x00
#define REG_SHUNT_V     0x01
#define REG_CALIBRATION 0x05

// ── HX711 ───────────────────────────────────────────────────────
#define HX711_DT     14
#define HX711_SCK    15
#define ALPHA        0.05f

// ── PWM / motor ─────────────────────────────────────────────────
#define IN1_PIN      0
#define IN2_PIN      1
#define PWM_MAX      4095
#define PWM_CLAMP    3000

// ── ADC / pot ───────────────────────────────────────────────────
#define POT_PIN      26
#define POT_MIN      1600
#define POT_MAX      3000

// ── haptic bump parameters (match haptic_curve.py) ───────────────
#define ANGLE_MIN       226.0f
#define ANGLE_MAX       419.0f
#define BLADE_CENTER_1  332.5f
#define BLADE_CENTER_2  364.2f
#define BLADE_CENTER_3  395.8f
#define BLADE_CENTER_4  427.5f  // just past max, barely felt
#define BLADE_WIDTH     12.0f
#define MAX_CURRENT_MA  250.0f  // max haptic force in mA

// ── PI controller parameters ─────────────────────────────────────
#define KP   0.05f
#define KI   0.00001f

// ── PI state (shared between interrupt and main) ─────────────────
volatile float desired_current_mA = 0.0f;
volatile float integral            = 0.0f;
volatile bool  safety_kill         = false;

// ── AS5600 ──────────────────────────────────────────────────────
uint16_t as5600_read_angle() {
    uint8_t reg = REG_ANGLE_H;
    uint8_t buf[2];
    i2c_write_blocking(I2C_PORT, AS5600_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, AS5600_ADDR, buf, 2, false);
    return ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
}

// ── INA219 ──────────────────────────────────────────────────────
void ina219_write(uint8_t reg, uint16_t val) {
    uint8_t buf[3];
    buf[0] = reg;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = val & 0xFF;
    i2c_write_blocking(I2C_PORT, INA219_ADDR, buf, 3, false);
}

int16_t ina219_read(uint8_t reg) {
    uint8_t buf[2];
    i2c_write_blocking(I2C_PORT, INA219_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, INA219_ADDR, buf, 2, false);
    return (int16_t)((buf[0] << 8) | buf[1]);
}

void ina219_init() {
    ina219_write(REG_CALIBRATION, 4096);
    ina219_write(REG_CONFIG, 0x39FF);
}

// ── HX711 ───────────────────────────────────────────────────────
void init_hx711() {
    gpio_init(HX711_SCK);
    gpio_set_dir(HX711_SCK, GPIO_OUT);
    gpio_put(HX711_SCK, 0);
    gpio_init(HX711_DT);
    gpio_set_dir(HX711_DT, GPIO_IN);
}

int32_t read_hx711() {
    uint32_t raw = 0;
    while (gpio_get(HX711_DT) == 1) { tight_loop_contents(); }
    for (int i = 0; i < 24; i++) {
        gpio_put(HX711_SCK, 1); sleep_us(1);
        raw = (raw << 1) | gpio_get(HX711_DT);
        gpio_put(HX711_SCK, 0); sleep_us(1);
    }
    gpio_put(HX711_SCK, 1); sleep_us(1);
    gpio_put(HX711_SCK, 0); sleep_us(1);
    if (raw & 0x800000) { raw |= 0xFF000000; }
    return (int32_t)raw;
}

// ── PWM ─────────────────────────────────────────────────────────
void pwm_init_pin(uint pin) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_wrap(slice, PWM_MAX);
    pwm_set_enabled(slice, true);
}

void set_motor(int duty) {
    if (duty >  PWM_CLAMP) duty =  PWM_CLAMP;
    if (duty < -PWM_CLAMP) duty = -PWM_CLAMP;
    if (duty > 0) {
        pwm_set_gpio_level(IN1_PIN, duty);
        pwm_set_gpio_level(IN2_PIN, PWM_MAX);
    } else if (duty < 0) {
        pwm_set_gpio_level(IN1_PIN, PWM_MAX);
        pwm_set_gpio_level(IN2_PIN, -duty);
    } else {
        pwm_set_gpio_level(IN1_PIN, 0);
        pwm_set_gpio_level(IN2_PIN, 0);
    }
}

// ── gaussian bump lookup ─────────────────────────────────────────
// returns normalized force 0.0 to 1.0 based on angle
float bump_force(float angle) {
    float centers[4] = {
        BLADE_CENTER_1, BLADE_CENTER_2,
        BLADE_CENTER_3, BLADE_CENTER_4
    };
    float force = 0.0f;
    for (int i = 0; i < 4; i++) {
        float diff = (angle - centers[i]) / BLADE_WIDTH;
        force += expf(-0.5f * diff * diff);
    }
    // clamp to 0-1
    if (force > 1.0f) force = 1.0f;
    if (force < 0.0f) force = 0.0f;
    return force;
}

// ── 1kHz PI current controller interrupt ────────────────────────
bool repeating_timer_callback(struct repeating_timer *t) {
    if (safety_kill) {
        set_motor(0);
        integral = 0.0f;
        return true;
    }

    // read actual current
    int16_t shunt_raw = ina219_read(REG_SHUNT_V);
    float actual_mA   = (shunt_raw * 0.01f) / 0.1f;

    // PI controller
    float error  = desired_current_mA - actual_mA;
    integral    += error;
    float output = KP * error + KI * integral;

    set_motor((int)output);
    return true;
}

// ── Main ────────────────────────────────────────────────────────
int main() {
    stdio_init_all();

    // I2C init
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    ina219_init();

    // HX711 init
    init_hx711();

    // ADC init
    adc_init();
    adc_gpio_init(POT_PIN);

    // PWM init
    pwm_init_pin(IN1_PIN);
    pwm_init_pin(IN2_PIN);
    set_motor(0);

    // start 1kHz PI controller interrupt
    struct repeating_timer timer;
    add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);

    // angle unwrap state
    float prev_angle = -1.0f;
    float offset     = 0.0f;
    float last_angle = (ANGLE_MIN + ANGLE_MAX) / 2.0f;

    // force filter state
    float filtered_force = 0.0f;
    int   first          = 1;

    sleep_ms(2000);
    printf("Ready!\n");

    while (1) {
        // check for reset command from Python
        int c = getchar_timeout_us(0);
        if (c == 'r') {
            prev_angle = -1.0f;
            offset     = 0.0f;
        }

        // ── pot safety cutoff ────────────────────────────────────
        adc_select_input(0);
        uint16_t pot = adc_read();
        if (pot < POT_MIN || pot > POT_MAX) {
            safety_kill        = true;
            desired_current_mA = 0.0f;
        } else {
            safety_kill = false;
        }

        // ── read angle ───────────────────────────────────────────
        uint16_t raw_angle = as5600_read_angle();
        float degrees = raw_angle * 360.0f / 4096.0f;

        if (prev_angle < 0.0f) { prev_angle = degrees; }
        float diff = degrees - prev_angle;
        if (diff > 180.0f)       { offset -= 360.0f; }
        else if (diff < -180.0f) { offset += 360.0f; }
        float unwrapped = degrees + offset;
        prev_angle = degrees;

        if (unwrapped > 600.0f) { offset -= 360.0f; unwrapped -= 360.0f; }
        if (unwrapped < 100.0f) { offset += 360.0f; unwrapped += 360.0f; }

        // ── haptic force calculation ──────────────────────────────
        // only resist when moving INTO the grass (angle increasing)
        float moving_right = unwrapped - last_angle;
        last_angle = unwrapped;

        float norm_force = bump_force(unwrapped);

        if (moving_right > 0.0f && unwrapped > (ANGLE_MIN + ANGLE_MAX) / 2.0f) {
            // moving into grass zone — push back (negative = push left)
            desired_current_mA = -norm_force * MAX_CURRENT_MA;
        } else {
            // moving left or in free zone — no resistance
            desired_current_mA = 0.0f;
            integral           = 0.0f;
        }

        // ── read force from load cell ────────────────────────────
        int32_t raw_force = read_hx711();
        if (first) { filtered_force = (float)raw_force; first = 0; }
        else { filtered_force = ALPHA * (float)raw_force + (1.0f - ALPHA) * filtered_force; }

        // ── stream to Python ─────────────────────────────────────
        printf("%.1f,%.1f,%.1f\n", unwrapped, filtered_force,
               desired_current_mA);
    }
}