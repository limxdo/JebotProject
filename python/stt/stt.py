#!/usr/bin/env python3

import json
import alsaaudio
from vosk import Model, KaldiRecognizer

def listen(model_path: str):
    SAMPLE_RATE = 16000
    CHANNELS = 1
    PERIOD_SIZE = 4000

    model = Model(model_path)
    recognizer = KaldiRecognizer(model, SAMPLE_RATE)

    pcm = alsaaudio.PCM(
        type=alsaaudio.PCM_CAPTURE,
        mode=alsaaudio.PCM_NORMAL
    )

    pcm.setchannels(CHANNELS)
    pcm.setrate(SAMPLE_RATE)
    pcm.setformat(alsaaudio.PCM_FORMAT_S16_LE)
    pcm.setperiodsize(PERIOD_SIZE)

    print("Listening... Press Ctrl+C to stop.")

    try:
        while True:
            length, data = pcm.read()

            if length == 0:
                continue

            if recognizer.AcceptWaveform(data):
                result = json.loads(recognizer.Result())
                text = result.get("text", "")
                if text:
                    return text

    except KeyboardInterrupt:
        return None
