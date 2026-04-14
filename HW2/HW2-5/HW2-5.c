#include <stdio.h> // set pico_enable_stdio_usb to 1 in CMakeLists.txt 
#include "pico/stdlib.h" // CMakeLists.txt must have pico_stdlib in target_link_libraries
#include "hardware/pwm.h" // CMakeLists.txt must have hardware_pwm in target_link_libraries
#include "hardware/adc.h" // CMakeLists.txt must have hardware_adc in target_link_libraries
#define PWMPIN 16

// Set servo angle (0-180 degrees)
void servo_set_angle(uint pin, float angle) {
    if (angle < 0)   angle = 0;
    if (angle > 180) angle = 180;
    uint16_t level = (uint16_t)(1200 + (angle / 180.0f) * (2400 - 1200));
    pwm_set_gpio_level(pin, level);
}

int main()
{
    stdio_init_all();
    // turn on the pwm, in this example to 10kHz with a resolution of 1500
    gpio_set_function(PWMPIN, GPIO_FUNC_PWM); // Set the Pin to be PWM
    uint slice_num = pwm_gpio_to_slice_num(PWMPIN); // Get PWM slice number
    // the clock frequency is 150MHz divided by a float from 1 to 255
    float div = 125; // must be between 1-255
    pwm_set_clkdiv(slice_num, div); // sets the clock speed
    uint16_t wrap = 24000; // when to rollover, must be less than 65535
    pwm_set_wrap(slice_num, wrap); 
    pwm_set_enabled(slice_num, true); // turn on the PWM
    // turn on the adc
    adc_init();
    adc_gpio_init(26); // pin GP26 is pin ADC0
    adc_select_input(0); // sample from ADC0
    while (true) {
        // sweep from 0 to 180 degrees and back
        for (float angle = 0; angle <= 180; angle += 1.0f) {
            servo_set_angle(PWMPIN, angle);
            sleep_ms(15);
        }
        for (float angle = 180; angle >= 0; angle -= 1.0f) {
            servo_set_angle(PWMPIN, angle);
            sleep_ms(15);
        }
    }
}