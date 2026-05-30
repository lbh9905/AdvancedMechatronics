import time
import board
import digitalio

btn_left = digitalio.DigitalInOut(board.GP14)
btn_left.direction = digitalio.Direction.INPUT
btn_left.pull = digitalio.Pull.UP   # internal pull-up, no resistor needed

btn_right = digitalio.DigitalInOut(board.GP15)
btn_right.direction = digitalio.Direction.INPUT
btn_right.pull = digitalio.Pull.UP

while True:
    left  = 0 if btn_left.value  else 1   # value=False when pressed
    right = 0 if btn_right.value else 1
    print("(" + str(left) + "," + str(right) + ")")
    time.sleep(1/30)