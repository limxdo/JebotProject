#!/usr/bin/env python3

import stt # replace it with 'jebot.stt' later
import os
import sys
import signal
from time import sleep
import json

# constents
RUNTIME_PATH      = "run/jebot"
STTD_RUNTIME_PATH = RUNTIME_PATH + "/sttd"
STTD_TEXT_FILE    = STTD_RUNTIME_PATH + "/text"
STTD_LANG_FILE    = STTD_RUNTIME_PATH + "/lang"
STTD_DATA_FILE  = "simple_data.json"

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

    # shutting up the vosk log messges
    null = os.open("/dev/null", os.O_WRONLY)
    fd_back = os.dup(2)
    os.dup2(null, 2)
    stt.load_model("enm", Lang)
    os.dup2(fd_back, 2)
    os.close(null)
except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr)
    sys.exit(1)


while running:
    try:
        sleep(0.5)
        text = stt.listen("en", 10, costom_words=custom_words)
        if isinstance(text, str): #and MAGIC_WORD in raw_text.split():
            print("new text detected.")
            write_text(data[stt.simple(text, data)])
        else:
            write_text("");
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)

# exit
try:
    text_file.close()
    os.remove(STTD_TEXT_FILE)
    os.remove(STTD_LANG_FILE)
    os.rmdir(STTD_RUNTIME_PATH)

    try: os.rmdir(RUNTIME_PATH)
    except OSError: pass # if other programs uses this path
except Exception: exit_status = 1

sys.exit(exit_status)
