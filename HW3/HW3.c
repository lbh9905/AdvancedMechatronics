#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "pico/cyw43_arch.h"

#define ADDR 0x20
#define IODIR 0x00
#define GPIO_REG 0x09
#define OLAT 0x0A

#define HEARTBEAT_PIN CYW43_WL_GPIO_LED_PIN

void setPin(unsigned char address, unsigned char reg, unsigned char value) {
    unsigned char buf[2];
    buf[0] = reg;
    buf[1] = value;
    i2c_write_blocking(i2c_default, address, buf, 2, false);
}

unsigned char readPin(unsigned char address, unsigned char reg) {
    unsigned char buf;
    i2c_write_blocking(i2c_default, address, &reg, 1, true);
    i2c_read_blocking(i2c_default, address, &buf, 1, false);
    return buf;
}

int main() {
    stdio_init_all();
    cyw43_arch_init();

    i2c_init(i2c_default, 100 * 1000);
    gpio_set_function(0, GPIO_FUNC_I2C); // SDA
    gpio_set_function(1, GPIO_FUNC_I2C); // SCL

    // Initialize MCP23008
    setPin(ADDR, IODIR, 0b00000001); // GP7 output, GP0 input

    while (1) {
        // Heartbeat
        cyw43_arch_gpio_put(HEARTBEAT_PIN, 1);
        sleep_ms(500);
        cyw43_arch_gpio_put(HEARTBEAT_PIN, 0);
        sleep_ms(500);

        // Read button on GP0
        unsigned char gpioState = readPin(ADDR, GPIO_REG);
        unsigned char buttonPressed = !(gpioState & 0b00000001);

        printf("gpioState: %d, buttonPressed: %d\n", gpioState, buttonPressed);

        // Control LED on GP7
        if (buttonPressed) {
            printf("LED ON\n");
            setPin(ADDR, OLAT, 0b10000000);
        } else {
            printf("LED OFF\n");
            setPin(ADDR, OLAT, 0b00000000);
        }
    }
}