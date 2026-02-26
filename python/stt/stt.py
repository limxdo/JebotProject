#!/usr/bin/env python3

import json
import alsaaudio
from vosk import Model, KaldiRecognizer
import os

models_dict = {}
def load_model(model_path: str, model_name: str):
    global modeles_dict

    if not os.path.exists(model_path):
        raise FileNotFoundError(f"model: '{model_path}' not found.")

    models_dict[model_name] = Model(model_path)

def listen(model_name: str, timeout=0,noise=8000):
    global models_dict
    modifier = 0.25
    if not timeout:
        timeout = 1
        modifier = 0

    sample_rate = 16000
    channels = 1
    period_size = 16000

    if model_name not in models_dict:
        raise ValueError(f"model: '{model_name}' not loaded")

    recognizer = KaldiRecognizer(models_dict[model_name], sample_rate)
       
    pcm = alsaaudio.PCM(
        type=alsaaudio.PCM_CAPTURE,
        mode=alsaaudio.PCM_NORMAL,
        device="plug:default"
    )

    pcm.setchannels(channels)
    pcm.setrate(sample_rate)
    pcm.setformat(alsaaudio.PCM_FORMAT_S16_LE)
    pcm.setperiodsize(period_size)

    print("listening... press ctrl+c to stop.")

    try:
        while timeout: 
            timeout -= modifier
            length, data = pcm.read()

            if length == 0:
                continue

            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                text = result.get("text", "")
                if text:
                    pcm.close()
                    return text

    except KeyboardInterrupt:
        return None

# function to get a strongest key in 'data'
def simple(value: str, data: dict):

    high = 0                 # biggest strong for keys in the outdata
    outdata = {}             # out data from data: dict
    value = value.split()

    for keys in data:
        memory = 0 # for the keys strong

        for val in value:
            for key in keys.split():
                if val == key:
                    memory += 1 # make the key stronger
            outdata[keys] = memory # set data in the outdata

    for key in outdata:         # <<
        if outdata[key] > high: # << get max strong in outdata and set in the 'high' variable
            high = outdata[key] # << 

    if not high:    # if the 'value' is not in 'data'
        return None

    for key in outdata:          # <<
        if outdata[key] == high: # << take the strongest key in 'outdata'
            return key           # <<

# example to use

# data = {
#     "go to manager room": "GOTO_MANAGER-ROOM",
#     "go to first class room": "GOTO_FIRST-CLASS",
#     "go to second class room": "GOTO_SECOND-CLASS",
#     "go to third class room": "GOTO_THIRD-CLASS",
# }

# key = simple('manager', data) # return: go to manager room
# print(data[key] if key else None) # GOTO_MANAGER-ROOM

