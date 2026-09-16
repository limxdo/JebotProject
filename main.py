#!/usr/bin/env python3

import sys
sys.dont_write_bytecode = True

import os
import signal
from time import sleep
import json

# Constants
# jebot data paths
CACHE_PATH = "/var/cache/jebot"
VAR_PATH   = "/var/lib/jebot"
SHARE_PATH = "/usr/local/share/jebot" # read only data

# general
USER_COMMANDS_FILE = SHARE_PATH + "/user_commands.json"
BASE_SOUNDS_PATH = SHARE_PATH + "/sounds"

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

# sttd
STTD_TEXT_FILE  = "/run/jebot/sttd/text"
STTD_LANG_FILE  = "/run/jebot/sttd/lang"


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

    # function to get user command from sttd
    def get_user_command():
        with open(STTD_TEXT_FILE, 'r') as f:
            cmd = f.read().strip()

        # temporary method to delete the command after reading it (this will be changed later).
        with open(STTD_TEXT_FILE, 'w') as f:
            f.write("")

        if cmd: return cmd
        else: return None

    def get_current_lang():
        with open(STTD_LANG_FILE, 'r') as f:
            return f.read().strip()

    current_lang = get_current_lang()
    sounds_path = BASE_SOUNDS_PATH + f"/{current_lang}"

    # function to play sound from sounds_path using aplay as child process without wait
    def play_sound(sound_name:str, device="default") -> int:
        if not os.path.exists(f"{sounds_path}/{sound_name}"):
            raise FileNotFoundError(f"sound: {sounds_path}/{sound_name} not found")

        pid = os.fork()

        if pid == 0:
            null = os.open("/dev/null", os.O_RDWR)

            os.dup2(null, 0)
            os.dup2(null, 1)
            os.dup2(null, 2)

            os.execlp("aplay", "aplay", "-q", "-D", device, f"{sounds_path}/{sound_name}")
            os._exit(1)
        else:
            return pid

    # load USER_COMMANDS_FILE
    with open(USER_COMMANDS_FILE, "r") as f:
        user_commands = json.load(f)

    # create paths
    if not os.path.exists(CACHE_PATH):
        os.mkdir(CACHE_PATH, 0o755)
    os.chmod(CACHE_PATH, 0o755)

    if not os.path.exists(VAR_PATH):
        os.mkdir(VAR_PATH, 0o755)
    os.chmod(VAR_PATH, 0o755)

    # vars
    child_pid = None
    last_user_cmd = None

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


# main loop, any exceptions based on 'Exception' here just be print as error log
while running:
    try:
        current_lang = get_current_lang()
        sounds_path = BASE_SOUNDS_PATH + f"/{current_lang}"
        user_cmd = get_user_command()

        if user_cmd:
            if last_user_cmd != user_cmd:
                if user_cmd in user_commands:
                    print(f"user command: {user_cmd}", flush=True)
                    sound = user_commands[user_cmd]["sound"]
                    point = user_commands[user_cmd]["point"]
                    if sound:
                        print(f"sound: {sound}", flush=True)
                        if child_pid:
                            os.kill(child_pid, signal.SIGTERM)
                            os.wait()
                            child_pid = None

                        child_pid = play_sound(sound)

                else:
                    print(f"{user_cmd} not in user_commands", file=sys.stderr, flush=True)
                last_user_cmd = user_cmd
        else:
            last_user_cmd = user_cmd

        # clean child process if finished
        if child_pid and os.waitpid(child_pid, os.WNOHANG)[0]: child_pid = None

        sleep(0.5)

    except Exception as e:
        print(f"error: {e}", file=sys.stderr, flush=True)


# exiting / cleaning here

sys.exit(0)
