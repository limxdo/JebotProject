#!/usr/bin/env python3

import sys
sys.dont_write_bytecode = True

import stt
import os
import signal
from time import sleep
import json

# Base Paths
RUNTIME_PATH      = "/run/jebot"
STT_DATA_PATH     = "/usr/local/share/jebot/stt"

# Fork Paths
STTD_RUNTIME_PATH = RUNTIME_PATH + "/sttd"
STTD_TEXT_FILE    = STTD_RUNTIME_PATH + "/text"
STTD_LANG_FILE    = STTD_RUNTIME_PATH + "/lang"
STT_MODELS_PATH   = STT_DATA_PATH + "/models"
STTD_DATA_FILE    = STT_DATA_PATH + "/simple_data.json"

# Varibles
MAGIC_WORD="listen"

try:
    running = True

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

    custom_words = set()

    for keys in data.keys():
        for words in keys.split():
            custom_words.add(words)

    custom_words = list(custom_words)

    exit_status = 0
    Lang = "en"

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

    write_text("") # just create file
    os.chmod(STTD_TEXT_FILE, 0o644)

    with open(STTD_LANG_FILE, "w") as lang_file:
        lang_file.write(Lang + "\n")

    # check model
    if not os.path.exists(f"{STT_MODELS_PATH}/{Lang}"):
        raise FileNotFoundError(f"model {Lang} not found")

    # load model
    null = os.open("/dev/null", os.O_WRONLY)
    fd_back = os.dup(2)
    os.dup2(null, 2)
    stt.load_model(f"{STT_MODELS_PATH}/{Lang}", Lang)
    os.dup2(fd_back, 2)
    os.close(null)
except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


while running:
    try:
        while True:
            RandomText = stt.listen(Lang, 10, costom_words=[MAGIC_WORD])
            if isinstance(RandomText, str):
                break

        if MAGIC_WORD in RandomText:
            print("listening", flush=True)
            sleep(0.5)
            text = stt.listen(Lang, 10, costom_words=custom_words)
            if isinstance(text, str):
                print(f"new text detected: {text}", flush=True)

                key = stt.simple(text, data)
                if key:
                    print(data[key], flush=True)
                    write_text(f"{data[key]}\n")
                else:
                    write_text("NOT_UNDERSTAND\n")
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
