#!/usr/bin/env python3

import json
import alsaaudio
from vosk import Model, KaldiRecognizer, SetLogLevel
import os
import time

SetLogLevel(-1)

models_dict = {}
def load_model(model_path: str, model_name: str):
    global modeles_dict

    if not os.path.exists(model_path):
        raise FileNotFoundError(f"model: '{model_path}' not found.")

    models_dict[model_name] = Model(model_path)

def del_model(model_name:str):
    global models_dict
    
    if model_name not in models_dict:
        raise ValueError(f"model: '{model_name}' not found.")

    del models_dict[model_name]

TimeEnd=0
Model_name=None
def listen(model_name: str, timeout=0, sample_rate_in=16000, costom_words=None, Device="plug:default", period_size=16000, channels=1): # costom_words is list!
    global Model_name
    global TimeEnd
    global models_dict
    
    Model_name = model_name

    if model_name not in models_dict:
        raise ValueError(f"model: \"{model_name}\" not loaded, please load model!")

    # Processing the model
    # vvvvvvvvvvvvvvvvvvvv
    if costom_words!=None:
        costom_words = json.dumps(costom_words)
        recognizer = KaldiRecognizer(models_dict[model_name], sample_rate_in, costom_words)
    else:
        recognizer = KaldiRecognizer(models_dict[model_name], sample_rate_in)
    # ^^^^^^^^^^^^^^^^^^^^
    #      processed.
    pcm = alsaaudio.PCM(
    type=alsaaudio.PCM_CAPTURE,
    mode=alsaaudio.PCM_NORMAL,
    device=Device,
    channels=channels,
    rate=sample_rate_in,
    format=alsaaudio.PCM_FORMAT_S16_LE,
    periodsize=period_size
    )

    TimeEnd = time.time() + timeout

    try:
        while TimeEnd > time.time(): 

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

def cal(value: str, value2 : str): # returns ratio of similarity
    index_mem = 0
    score = 0
    if len(value)<=len(value2):
        try:
            for char in value:
                if value[index_mem-1]==value2[index_mem] or\
                    value[index_mem+1]==value2[index_mem] or\
                    value[index_mem]==value2[index_mem]:
                    score+=1
                index_mem+=1
        except:
            index_mem+=1
    elif len(value)>=len(value2):
        try:
            for char in value2:
                if value[index_mem-1]==value2[index_mem] or\
                    value[index_mem+1]==value2[index_mem] or\
                    value[index_mem]==value2[index_mem]:
                    score+=1
                index_mem+=1
        except:
            index_mem+=1
    return (score/index_mem)*100

def simple(value: str, data: dict, miniweight=75.0) :
    high = 0                 # biggest strong for keys in the outdata
    outdata = {}             # out data from data: dict
    clean_value = value
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

    highers=0
    for key in outdata:          # <<
        if outdata[key] == high: # << take the strongest key in 'outdata'
            highers+=1
    if highers==1:
        for key in outdata:
            if outdata[key] == high:
                return key
    elif highers>1:             # will search in highers to take it char by char simple
        highers_list = []
        for keys in outdata:
            if outdata[keys] == high and(not keys in highers_list):
                highers_list.append(keys)
        higher_char = 0
        weight = 0
        for highers_in in highers_list:
            if weight < cal(highers_in, clean_value):
                weight = cal(highers_in, clean_value)
                
        print(weight)
        if weight>=miniweight:
            for highers_in in highers_list:
                if weight == cal(highers_in, clean_value):
                    print(weight)
                    return highers_in
# example to use
# vvvvvvvvvvvvvv

# data = {
#     "go to manager room": "GOTO_MANAGER_ROOM",
#     "go to first class room": "GOTO_FIRST_CLASS",
#     "go to second class room": "GOTO_SECOND_CLASS",
#     "go to third class room": "GOTO_THIRD_CLASS",
# }
# 
# costom_words_list = [ "where", "is", "the", "manager", "first", "second", "third", "room", "class", "classroom" ]
# load_model("enm", "md")
# listent = listen("md", 30, 16000)
# print(listent)
# key = simple(listent, data)
# if key != "None":
#   print(data[key])
