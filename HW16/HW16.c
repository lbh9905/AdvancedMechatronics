#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/i2c.h"
#include "pico/time.h"

// ── I2C / INA219 ────────────────────────────────────────────────
#define I2C_PORT        i2c0
#define SDA_PIN         4
#define SCL_PIN         5
#define INA219_ADDR     0x40
#define REG_CONFIG      0x00
#define REG_SHUNT_V     0x01
#define REG_CALIBRATION 0x05

// ── PWM / motor ─────────────────────────────────────────────────
#define IN1_PIN         0
#define IN2_PIN         1
#define PWM_MAX         4095

// ── ADC / pot ───────────────────────────────────────────────────
#define POT_PIN         26
#define POT_MIN         1600    // updated from your new servo horn position
#define POT_MAX         3000    // updated from your new servo horn position

// ── PI tuning ───────────────────────────────────────────────────
#define KP   0.02f
#define KI   0.00001f
#define TARGET_MA       500.0f
#define PWM_CLAMP       3000

// ── data collection ─────────────────────────────────────────────
#define NUM_SAMPLES     400
float desired_log[NUM_SAMPLES];
float actual_log[NUM_SAMPLES];
int   sample_count = 0;
bool  running      = false;

// ── PI state ────────────────────────────────────────────────────
float integral   = 0.0f;
float desired_mA = TARGET_MA;

// ── INA219 helpers ──────────────────────────────────────────────
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

// ── PWM helpers ─────────────────────────────────────────────────
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

// ── 1kHz timer interrupt ────────────────────────────────────────
bool repeating_timer_callback(struct repeating_timer *t) {
    if (!running) return true;

    // 1. check pot safety cutoff
    adc_select_input(0);
    uint16_t pot = adc_read();
    if (pot < POT_MIN || pot > POT_MAX) {
        set_motor(0);
        integral = 0.0f;
        return true;
    }

    // 2. flip desired current every 100 samples (100ms)
    if (sample_count % 100 == 0) {
        desired_mA = -desired_mA;
    }

    // 3. read actual current from INA219
    int16_t shunt_raw = ina219_read(REG_SHUNT_V);
    float actual_mA   = (shunt_raw * 0.01f) / 0.1f;

    // 4. PI controller
    float error  = desired_mA - actual_mA;
    integral    += error;
    float output = KP * error + KI * integral;

    // 5. drive motor
    set_motor((int)output);

    // 6. log data
    if (sample_count < NUM_SAMPLES) {
        desired_log[sample_count] = desired_mA;
        actual_log[sample_count]  = actual_mA;
        sample_count++;
    } else {
        set_motor(0);
        integral = 0.0f;
        running  = false;
    }

    return true;
}

// ── main ────────────────────────────────────────────────────────
int main() {
    stdio_init_all();

    // ADC init
    adc_init();
    adc_gpio_init(POT_PIN);

    // I2C init
    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);
    ina219_init();

    // PWM init
    pwm_init_pin(IN1_PIN);
    pwm_init_pin(IN2_PIN);
    set_motor(0);

    // start 1kHz repeating timer
    struct repeating_timer timer;
    add_repeating_timer_ms(-1, repeating_timer_callback, NULL, &timer);

    sleep_ms(2000);

    while (1) {
        // keep announcing ready until Python responds
        printf("Ready!\n");
        sleep_ms(500);

        int c = getchar_timeout_us(10000);
        if (c == 'a' && !running) {
            sample_count = 0;
            integral     = 0.0f;
            desired_mA   = TARGET_MA;
            running      = true;
            printf("Running...\n");
        }

        // once run is done, send data over serial
        if (!running && sample_count == NUM_SAMPLES) {
            printf("DATA START\n");
            for (int i = 0; i < NUM_SAMPLES; i++) {
                printf("%.1f,%.1f\n", desired_log[i], actual_log[i]);
            }
            printf("DATA END\n");
            sample_count = 0;
        }
    }
}