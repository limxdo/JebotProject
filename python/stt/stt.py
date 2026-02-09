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

def listen(model_name: str, timeout=0):
    global models_dict
    modifier = 0.25
    if not timeout:
        timeout = 1
        modifier = 0

    sample_rate = 16000
    channels = 1
    period_size = 4000

    if model_name not in models_dict:
        raise ValueError(f"model: '{model_name}' not loaded")

    recognizer = KaldiRecognizer(models_dict[model_name], sample_rate)
       
    pcm = alsaaudio.PCM(
        type=alsaaudio.PCM_CAPTURE,
        mode=alsaaudio.PCM_NORMAL
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
