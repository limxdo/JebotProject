#!/usr/bin/env python3

import sys
sys.dont_write_bytecode = True

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
STTD_TEXT_FILE      = STTD_RUNTIME_PATH + "/text"
STTD_LANG_FILE      = STTD_RUNTIME_PATH + "/lang"
STTD_LAST_LANG_FILE = VAR_PATH + "/last_lang"
STT_MODELS_PATH     = STT_DATA_PATH + "/models"
STTD_DATA_FILE      = STT_DATA_PATH + "/simple_data.json"

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

    # create STTD_TEXT_FILE and write NOTHING
    def write_text(text):
        with open(STTD_TEXT_FILE, "w") as Text_file:
            Text_file.write(text)

    if os.path.exists(STTD_LAST_LANG_FILE):
        with open(STTD_LAST_LANG_FILE, 'r') as f:
            Lang = f.read().strip()

    if Lang != "en" and Lang != "ar":
        Lang = "en"
        with open(STTD_LAST_LANG_FILE, 'w') as f:
            f.write(Lang + '\n')

    with open(STTD_LANG_FILE, 'w') as f:
        f.write(Lang + '\n')

    for keys in data["en"]:
        for words in keys.split():
            custom_words_en.add(words)

    custom_words_en = list(custom_words_en)


    write_text("") # just create file
    os.chmod(STTD_TEXT_FILE, 0o644)

    # check model
    if not os.path.exists(f"{STT_MODELS_PATH}/{Lang}"):
        raise FileNotFoundError(f"model {Lang} not found")

    # load model
    stt.load_model(f"{STT_MODELS_PATH}/{Lang}", Lang)

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


while running:
    try:
        sleep(0.3)

        RandomText = None
        text = None

        print(f"new listen with lang '{Lang}'", flush=True)
        if Lang == "en":
            RandomText = stt.listen(Lang, 10, costom_words=custom_words_en)
        elif Lang == "ar":
            RandomText = stt.listen(Lang, 10)

        if isinstance(RandomText, str):
            print(f"RandomText: {RandomText}", flush=True)
            key = stt.simple(RandomText, data[Lang])
            if key and data[Lang][key] == "MAGIC_WORD":
                print("listening", flush=True)
                if Lang == "en":
                    text = stt.listen(Lang, 10, costom_words=custom_words_en)
                elif Lang == "ar":
                    text = stt.listen(Lang, 10)
                if isinstance(text, str):
                    print(f"new text detected: {text}", flush=True)

                    key = stt.simple(text, data[Lang])
                    if key and key in data[Lang]:
                        print(data[Lang][key], flush=True)
                        write_text(f"{data[Lang][key]}\n")

                        if data[Lang][key] == "CHANGE_LANG":
                            stt.del_model(Lang)
                            if Lang == "ar":
                                print(f"changed language from {Lang} to en", flush=True)
                                Lang = "en"
                            elif Lang == "en":
                                print(f"changed language from {Lang} to ar", flush=True)
                                Lang = "ar"

                            stt.load_model(f"{STT_MODELS_PATH}/{Lang}", Lang)
                            with open(STTD_LANG_FILE, 'w') as f:
                                f.write(Lang + '\n')
                            with open(STTD_LAST_LANG_FILE, 'w') as f:
                                f.write(Lang + '\n')

                    else:
                        write_text("NOT_UNDERSTAND\n")
                else:
                    write_text("");
            else:
                write_text("");
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr, flush=True)

# exit
try:
    os.remove(STTD_TEXT_FILE)
    os.remove(STTD_LANG_FILE)
    os.rmdir(STTD_RUNTIME_PATH)

    try: os.rmdir(RUNTIME_PATH)
    except OSError: pass # if other programs uses this path
except Exception: exit_status = 1

sys.exit(exit_status)
