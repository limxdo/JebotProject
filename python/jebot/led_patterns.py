#!/usr/bin/env python3

import time

# jebot led here ^^
# made by Rfg6e :3

FRAME = 0.02

def set_led(r, g, b):
    # jebots real led pins go here for now it just prints the colors :3
    print(f"{r} {g} {b}")


def pulse(r, g, b, speed):
    step = 0
    while True:
        x = (step * speed) % 510
        if x > 255:
            x = 510 - x
        x = x * x // 255  # squared so jebot fades smooth and not ugly >_<
        lvl = 10 + x * 245 // 255  # jebot never fully sleeps just gets deep or smth idk
        set_led(r * lvl // 255, g * lvl // 255, b * lvl // 255)
        time.sleep(FRAME)
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
        time.sleep(FRAME)
        step = (step + 1) % 128


def hearing():
    pulse(255, 200, 0, 3)  # slow yellow breathing bc jebot is listening owo


def moving():
    pulse(0, 0, 255, 5)  # blue and a bit faster bc jebot is going places ^^


def error():
    set_led(255, 0, 0)  # jebot messed up >_<


def off():
    set_led(0, 0, 0)
