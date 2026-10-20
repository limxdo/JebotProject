#!/usr/bin/env python3

import sys
sys.dont_write_bytecode = True

# remove script's directory so `import jebot` finds the package, not 'jebot.py' file
# (jebot.py shadows jebot/ otherwise)
del sys.path[0]

import jebot.stt as stt
import os
import signal
from time import sleep
import json

# Base Paths
RUNTIME_PATH      = "/run/jebot"
STT_DATA_PATH     = "/usr/local/share/jebot/stt"
VAR_PATH          = "/var/lib/jebot"

# Fork Paths
STTD_RUNTIME_PATH   = RUNTIME_PATH + "/sttd"
STTD_PID_PATH       = STTD_RUNTIME_PATH + "/pid"
STTD_WAKEUP_FILE    = STTD_RUNTIME_PATH + "/wakeup"
STTD_TEXT_FILE      = STTD_RUNTIME_PATH + "/text"
STTD_LANG_FILE      = STTD_RUNTIME_PATH + "/lang"
STTD_LAST_LANG_FILE = VAR_PATH + "/last_lang"
STT_MODELS_PATH     = STT_DATA_PATH + "/models"
STTD_DATA_FILE      = STT_DATA_PATH + "/simple_data.json"

# wakeup
WAKEUP_DISTANCE = 30 # cm

try:
    running = True
    custom_words_en = set()

    # signal handler
    def handler(signum, frame):
        global running
        running = False
        stt.TimeEnd = 0 # stop listening

    # set signals function
    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)

    with open(STTD_DATA_FILE, "r") as f:
        data = json.load(f)

    exit_status = 0
    Lang = None

    # paths
    if not os.path.exists(RUNTIME_PATH):
        os.mkdir(RUNTIME_PATH, 0o755);
    os.chmod(RUNTIME_PATH, 0o755)

    if not os.path.exists(STTD_RUNTIME_PATH):
        os.mkdir(STTD_RUNTIME_PATH, 0o755)
    os.chmod(STTD_RUNTIME_PATH, 0o755)

    if not os.path.exists(VAR_PATH):
        os.mkdir(VAR_PATH, 0o755)
    os.chmod(VAR_PATH, 0o755)

    # wakeup
    def wakeup(wakeup:bool):
        with open(STTD_WAKEUP_FILE, 'w') as Wakeup:
            Wakeup.write(f"{int(wakeup)}\n")

    # create STTD_TEXT_FILE and write NOTHING
    def write_text(text):
        with open(STTD_TEXT_FILE, "w") as Text_file:
            Text_file.write(text)

    def ultrasonic_condition():
        try:
            with open("/run/jebot/ultrasonicd/front_right", 'r') as f:
                right = float(f.read().strip())
            with open("/run/jebot/ultrasonicd/front_left", 'r') as f:
                left = float(f.read().strip())
            with open("/run/jebot/motord/motors_state", 'r') as f:
                motors_state = f.read().strip()
        except FileNotFoundError:
            return False

        if ((right > 0.0 and right <= WAKEUP_DISTANCE) or (left > 0.0 and left <= WAKEUP_DISTANCE)) and motors_state != "running":
            wakeup(True)
            return True

        wakeup(False)
        return False

    if os.path.exists(STTD_LAST_LANG_FILE):
        with open(STTD_LAST_LANG_FILE, 'r') as f:
            Lang = f.read().strip()

    if Lang != "en" and Lang != "ar":
        Lang = "en"
        with open(STTD_LAST_LANG_FILE, 'w') as f:
            f.write(Lang + '\n')

    # write lang
    with open(STTD_LANG_FILE, 'w') as f:
        f.write(Lang + '\n')

    # write pid
    with open(STTD_PID_PATH, 'w') as f:
        f.write(f"{os.getpid()}\n")

    for keys in data["en"]:
        for words in keys.split():
            custom_words_en.add(words)

    custom_words_en = list(custom_words_en)


    # just create files
    write_text("")
    wakeup(False)
    os.chmod(STTD_TEXT_FILE, 0o644)
    os.chmod(STTD_WAKEUP_FILE, 0o644)

    # check model
    if not os.path.exists(f"{STT_MODELS_PATH}/{"en"}"):
        raise FileNotFoundError(f"model 'en' not found")

    if not os.path.exists(f"{STT_MODELS_PATH}/{"ar"}"):
        raise FileNotFoundError(f"model 'ar' not found")

    # load model
    stt.load_model(f"{STT_MODELS_PATH}/en", "en")
    stt.load_model(f"{STT_MODELS_PATH}/ar", "ar")

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


while running:
    try:
        sleep(0.01)
        RandomText = None

        if Lang == "en":
            RandomText = stt.listen(Lang, condition=ultrasonic_condition, costom_words=custom_words_en)
        elif Lang == "ar":
            RandomText = stt.listen(Lang, condition=ultrasonic_condition)
            
        if isinstance(RandomText, str):
            key = stt.simple(RandomText, data[Lang])
            if key and key in data[Lang]:
                print(data[Lang][key], flush=True)
                write_text(f"{data[Lang][key]}\n")

                if data[Lang][key] == "CHANGE_LANG":
                    if Lang == "ar":
                        print(f"changed language from {Lang} to en", flush=True)
                        Lang = "en"
                    elif Lang == "en":
                        print(f"changed language from {Lang} to ar", flush=True)
                        Lang = "ar"

                    with open(STTD_LANG_FILE, 'w') as f:
                        f.write(Lang + '\n')
                    with open(STTD_LAST_LANG_FILE, 'w') as f:
                        f.write(Lang + '\n')

            else:
                write_text("NOT_UNDERSTAND\n")
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)

# exit
try:
    os.remove(STTD_TEXT_FILE)
    os.remove(STTD_LANG_FILE)
    os.remove(STTD_WAKEUP_FILE)
    os.remove(STTD_PID_PATH)
    os.rmdir(STTD_RUNTIME_PATH)

    try: os.rmdir(RUNTIME_PATH)
    except OSError: pass # if other programs uses this path
except Exception: exit_status = 1

sys.exit(exit_status)
