#include "pico/stdlib.h"
#include "hardware/pwm.h"

// --- SETTINGS ---
#define SERVO_PIN 16        // Change this if your servo signal wire is on a different GPIO
#define PWM_FREQ 50        // 50Hz for servo
#define MIN_DUTY 2500      // 2.5% duty cycle = 0 degrees
#define MAX_DUTY 12500     // 12.5% duty cycle = 180 degrees

// This function sets the servo to a given duty cycle value
void set_servo(uint slice, uint chan, uint duty) {
    pwm_set_chan_level(slice, chan, duty);
}

int main() {
    stdio_init_all();

    // Set up the servo pin as a PWM pin
    gpio_set_function(SERVO_PIN, GPIO_FUNC_PWM);

    // Get the PWM "slice" and "channel" for our pin
    uint slice = pwm_gpio_to_slice_num(SERVO_PIN);
    uint chan  = pwm_gpio_to_channel(SERVO_PIN);

    // Configure PWM for 50Hz
    // Pico runs at 125MHz. We divide it down to get 50Hz with a wrap of 100000
    pwm_set_clkdiv(slice, 25.0f);     // 125MHz / 25 = 5MHz
    pwm_set_wrap(slice, 99999);       // 5MHz / 100000 = 50Hz

    pwm_set_enabled(slice, true);

    while (true) {
        // Sweep from 0 to 180 degrees
        for (uint duty = MIN_DUTY; duty <= MAX_DUTY; duty += 100) {
            set_servo(slice, chan, duty);
            sleep_ms(20);
        }

        // Sweep from 180 back to 0 degrees
        for (uint duty = MAX_DUTY; duty >= MIN_DUTY; duty -= 100) {
            set_servo(slice, chan, duty);
            sleep_ms(20);
        }
    }

    return 0;
}