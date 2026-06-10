#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#define I2C_PORT     i2c0
#define SDA_PIN      4
#define SCL_PIN      5
#define AS5600_ADDR  0x36
#define REG_ANGLE_H  0x0E
#define HX711_DT     14
#define HX711_SCK    15
#define ALPHA        0.05f

uint16_t as5600_read_angle() {
    uint8_t reg = REG_ANGLE_H;
    uint8_t buf[2];
    i2c_write_blocking(I2C_PORT, AS5600_ADDR, &reg, 1, true);
    i2c_read_blocking(I2C_PORT, AS5600_ADDR, buf, 2, false);
    return ((uint16_t)(buf[0] & 0x0F) << 8) | buf[1];
}

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
        gpio_put(HX711_SCK, 1);
        sleep_us(1);
        raw = (raw << 1) | gpio_get(HX711_DT);
        gpio_put(HX711_SCK, 0);
        sleep_us(1);
    }
    gpio_put(HX711_SCK, 1); sleep_us(1);
    gpio_put(HX711_SCK, 0); sleep_us(1);
    if (raw & 0x800000) { raw |= 0xFF000000; }
    return (int32_t)raw;
}

int main() {
    stdio_init_all();

    i2c_init(I2C_PORT, 100 * 1000);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN);
    gpio_pull_up(SCL_PIN);

    init_hx711();

    float filtered_force = 0.0f;
    int   first          = 1;
    float prev_angle     = -1.0f;
    float offset         = 0.0f;

    sleep_ms(2000);
    printf("Ready!\n");

    while (1) {
        // check for reset command from Python
        int c = getchar_timeout_us(0);
        if (c == 'r') {
            prev_angle = -1.0f;
            offset     = 0.0f;
        }

        // read angle
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

        // read force
        int32_t raw_force = read_hx711();
        if (first) { filtered_force = (float)raw_force; first = 0; }
        else { filtered_force = ALPHA * (float)raw_force + (1.0f - ALPHA) * filtered_force; }

        printf("%.1f,%.1f\n", unwrapped, filtered_force);
    }
}