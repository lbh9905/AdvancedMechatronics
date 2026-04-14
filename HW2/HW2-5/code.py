import time
import board
import pwmio
from adafruit_motor import servo

# Set up the servo on pin GP16
pwm = pwmio.PWMOut(board.GP16, frequency=50)
my_servo = servo.Servo(pwm)

# Sweep back and forth forever
while True:
    # Sweep from 0 to 180 degrees
    for angle in range(0, 181):
        my_servo.angle = angle
        time.sleep(0.015)  # small pause between each step

    # Sweep from 180 back to 0 degrees
    for angle in range(180, -1, -1):
        my_servo.angle = angle
        time.sleep(0.015)