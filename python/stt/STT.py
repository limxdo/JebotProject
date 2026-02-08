#!/usr/bin/env python3
# ============================================================
# OFFLINE SPEECH TO TEXT USING VOSK (LOCAL MODEL)
# FUNCTION-ONLY IMPLEMENTATION
# ============================================================

import json
import queue
import sounddevice as sd
from vosk import Model, KaldiRecognizer
from sys import argv

# ===================== USER CONFIG ===========================
MODEL_PATH = argv[1]

SAMPLE_RATE = 16000
CHANNELS = 1
# ===================== MODEL LOADING =========================

#def load_vosk_model(model_path: str):
#    return Model(model_path)

model = Model(MODEL_PATH)

# ===================== AUDIO QUEUE ===========================

audio_queue = queue.Queue()

# ===================== AUDIO CALLBACK ========================

def audio_callback(indata, frames, time_info, status):
    if status:
        print(status)
    audio_queue.put(bytes(indata))

# ===================== LISTEN & TRANSCRIBE ===================

def listen_and_transcribe():
    x = True
    recognizer = KaldiRecognizer(model, SAMPLE_RATE)

    print("Listening... Press Ctrl+C to stop.")

    with sd.RawInputStream(
        samplerate=SAMPLE_RATE,
        blocksize=8000,
        dtype="int16",
        channels=CHANNELS,
        callback=audio_callback
    ):
        try:
            while True:
                data = audio_queue.get()

                if recognizer.AcceptWaveform(data):
                    result = json.loads(recognizer.Result())
                    text = result.get("text", "").strip()
                    if text:
                        print(">>", text)
                        #return text
                        break
        except KeyboardInterrupt:
            print("Stopped.")

# ===================== ENTRY POINT ===========================


if __name__ == "__main__":
    listen_and_transcribe()

