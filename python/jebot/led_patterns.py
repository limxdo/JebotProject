#!/usr/bin/env python3

import time

# jebot led here ^^
# made by Rfg6e :3

# ledd
LEDD_CMD_FIFO = "/run/jebot/ledd/cmd"

_FRAME = 0.02

# led control using ledd
def set_led(r:int, g:int, b:int):
    if (r < 0 or r > 255) or (g < 0 or g > 255) or (b < 0 or b > 255):
        raise ValueError("values must be >= 0 and <= 255")

    with open(LEDD_CMD_FIFO, 'w') as f:
        f.write(f"{r} {g} {b}")


def _pulse(r, g, b, speed):
    step = 0
    while True:
        x = (step * speed) % 510
        if x > 255:
            x = 510 - x
        x = x * x // 255  # squared so jebot fades smooth and not ugly >_<
        lvl = 10 + x * 245 // 255  # jebot never fully sleeps just gets deep or smth idk
        set_led(r * lvl // 255, g * lvl // 255, b * lvl // 255)
        time.sleep(_FRAME)
        step = (step + 1) % 510


def talking():
    # jebot is yapping so all the colors come out ^^
    step = 0
    while True:
        p = (step * 2) % 256
        if p < 85:
            set_led(255 - p * 3, p * 3, 0)
        elif p < 170:
            p -= 85
            set_led(0, 255 - p * 3, p * 3)
        else:
            p -= 170
            set_led(p * 3, 0, 255 - p * 3)
        time.sleep(_FRAME)
        step = (step + 1) % 128


def hearing():
    _pulse(255, 200, 0, 3)  # slow yellow breathing bc jebot is listening owo


def moving():
    _pulse(0, 0, 255, 5)  # blue and a bit faster bc jebot is going places ^^


def error():
    set_led(255, 0, 0)  # jebot messed up >_<
    while True: pass


def off():
    set_led(0, 0, 0)
