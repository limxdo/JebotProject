#!/usr/bin/env python3

import os
import sys
import signal
from time import sleep

# Constants
# jebot data paths
CACHE_PATH = "/var/cache/jebot"
SHARE_PATH = "/usr/local/share/jebot" # read only data

# motord
MOTORD_CMD_FIFO    = "/run/jebot/motord/cmd"
MOTORD_REPLAY_FIFO = "/run/jebot/motord/reply"

# powerd
POWERD_INA219_VBUS    = "/run/powerd/ina219/bus_voltage"
POWERD_INA219_VSHUNT  = "/run/powerd/ina219/shunt_voltage"
POWERD_INA219_CURRENT = "/run/powerd/ina219/current"
POWERD_INA219_POWER   = "/run/powerd/ina219/power"

X1201_VOLTAGE  = "/run/powerd/x1201/voltage"
X1201_CAPACITY = "/run/powerd/x1201/capacity"

# ultrasonicd
ULTRASONICD_FRONT_RIGHT = "/run/jebot/ultrasonicd/front_right"
ULTRASONICD_FRONT_LEFT  = "/run/jebot/ultrasonicd/front_left"


# setup, any exceptions based on 'Exception' here is fatal
try:
    running = True # main loop condition

    # signal handler
    def handler(signum, frame):
        global running
        running = False

    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)

    # create paths
    os.mkdir(CACHE_PATH, 0o755)
    os.chmod(CACHE_PATH, 0o755)

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


# main loop, any exceptions based on 'Exception' here just be print as error log
while running:
    try:
        # process code here
        sleep(0.5)

    except Exception as e:
        print(f"error: {e}", file=sys.stderr, flush=True)


# exiting / cleaning here

sys.exit(0)
