#!/usr/bin/env python3

import sys

# remove python's cache & bytecode
sys.dont_write_bytecode = True

import os
import signal
from time import sleep
import json
import numpy as np
import jebot.control as control # fake

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
MOTORD_REPLY_FIFO = "/run/jebot/motord/reply"

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

# map
MAP_SHARE_PATH = SHARE_PATH + "/map"
MAP_SHARE_FILE = MAP_SHARE_PATH + "/map.json"
MAP_STATE_FILE = VAR_PATH + "/cache_points.json"

# constants for motord commands, for less RAM used
MOVE_FORWARD = 1
TURN_LEFT = 2
TURN_RIGHT = 3
MOVE_BACKWARD = 4

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

    # map
    def save_point(current_point:list,vector:str) -> None: # write current point in MAP_STATE_FILE
        dct_state = {
            "current_point":current_point,
            "vector":vector
        }
        with open(MAP_STATE_FILE,"w") as fp:
            json.dump(dct_state,fp=fp)

    def get_state() -> dict: # read from cache file
        if not os.path.exists(MAP_STATE_FILE):
            return None
        
        with open(MAP_STATE_FILE) as fp:
            return json.load(fp)
    

    def send(command : int,distance=40,angel=90) -> str:
        """func will send to pipe file ((FIFO) on unix & (based-like)) the command to motord.c """
        if os.path.exists(MOTORD_CMD_FIFO) and os.path.exists(MOTORD_REPLY_FIFO): 
            keys_convertors = {MOVE_FORWARD:f"MOVE_FORWARD {distance} --reply",TURN_LEFT:f"TURN_LEFT {angel} --reply",TURN_RIGHT:f"TURN_RIGHT {angel} --reply",MOVE_BACKWARD:f"MOVE_BACKWARD {distance} --reply" } # var contain all command and their num
            with open(MOTORD_CMD_FIFO,"w") as pipe: # open send pipe file
                pipe.write(keys_convertors[command]) # write the command on the pipe
            
            with open(MOTORD_REPLY_FIFO,"r") as pipe: # open reply pipe file
                replay = pipe.read() # read the reply
                replay.strip() # remove \n from the reply
                return replay
        else:
            print("error , pipes not exists.",file=sys.stderr,flush=True)

    def current_point(last_point:list,vector:str,last_command:str) -> tuple:
        # list near point from our point var
        dct_near_points = {"down" : (last_point[0]+1,last_point[1]), 
                            "right" : (last_point[0],last_point[1]+1), 
                            "up" : (last_point[0]-1,last_point[1]), 
                            "left" : (last_point[0],last_point[1]-1)} 

        match last_command: # use match-case to split the possibilities of commands 
            case "MOVE_FORWARD":
                if 0 > dct_near_points[vector][0] or 0 > dct_near_points[vector][1]: # check that the code will not get out from the map
                    return "error"

                return dct_near_points[vector],vector


            case "TURN_RIGHT": # handel all possibilities in if statement
                # we just here change the vector because there is no changing on real point
                if vector == "up":
                    return last_point,"right"
                elif vector == "down":
                    return last_point,"left"
                elif vector == "right":
                    return last_point,"down"
                elif vector == "left":
                    return last_point,"up"


            case "TURN_LEFT": # handel all possibilities in if statement
                # we just here change the vector because there is no changing on real point
                if vector == "up":
                    return last_point,"left"
                elif vector == "down":
                    return last_point,"right"
                elif vector == "right":
                    return last_point,"up"
                elif vector == "left":
                    return last_point,"down"


            case "MOVE_BACKWARD":
                # the bachward it just walk oppsite vector , so we use if statment here
                if vector == "up":
                    risult = dct_near_points["down"],vector
                elif vector == "down":
                    risult = dct_near_points["up"],vector
                elif vector == "right":
                    risult = dct_near_points["left"],vector
                elif vector == "left":
                    risult = dct_near_points["right"],vector
                
                if 0 > risult[0][0] or 0 > risult[0][1]: # check that the code will not geu out of the mao
                    return "error"
                
                return risult
            
            case _:
                return "error" # if the input is invalid


    # load USER_COMMANDS_FILE
    with open(USER_COMMANDS_FILE, "r") as f:
        user_commands = json.load(f)

    # read map.json file to get map of the plase & points
    with open(MAP_SHARE_FILE) as fp:
        dct_map = json.load(fp)
        dct_map["maps"]["school_hall"] = np.array(dct_map,dtype=np.int8)

    # create paths
    if not os.path.exists(CACHE_PATH):
        os.mkdir(CACHE_PATH, 0o755)
    os.chmod(CACHE_PATH, 0o755)

    if not os.path.exists(VAR_PATH):
        os.mkdir(VAR_PATH, 0o755)
    os.chmod(VAR_PATH, 0o755)

    # sound var
    child_pid = None

    # The order of the vars here is important !!! 
    goal_point = None 
    now_point = None
    vector = None
    dct_state = get_state() # get cuurant data from cache    

    # check if tjere is chache file
    if dct_state:
        vector = dct_state["vector"]
        now_point = dct_state["current_point"]
    else:
        print("there is no cache file (you can ignore it).",flush=True)

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


# main loop, any exceptions based on 'Exception' here just be print as error log
while running:
    try:
        current_lang = get_current_lang()
        sounds_path = BASE_SOUNDS_PATH + f"/{current_lang}"
        user_cmd = get_user_command()
        lst_map = dct_map["maps"]["school_hall"].copy() # get map from map.json

        if user_cmd:
            if user_cmd in user_commands:
                print(f"user command: {user_cmd}", flush=True)
                sound = user_commands[user_cmd]["sound"]

                if not goal_point:
                    goal_point = user_commands[user_cmd]["point"]
                    if goal_point:
                        goal_point = dct_map["points"][goal_point]

                if sound:
                    print(f"sound: {sound}", flush=True)
                    if child_pid:
                        os.kill(child_pid, signal.SIGTERM)
                        os.wait()
                        child_pid = None

                    child_pid = play_sound(sound)
            else:
                print(f"{user_cmd} not in user_commands", file=sys.stderr, flush=True)

        # clean child process if finished
        if child_pid and os.waitpid(child_pid, os.WNOHANG)[0]: child_pid = None

        # if we dont take data from cache file we will use default value
        if not now_point:
            now_point = dct_map["points"]["home"] # default value
        
        if not vector:
            vector = "up"  # default value

        if goal_point and not child_pid: # do not move while speaking
            while ((now_point[0] != goal_point[0]) or (now_point[1] != goal_point[1])) and running: 
                way = control.command(lst_points=control.path(Map=np.array(lst_map),start_point=now_point,end_point=goal_point),vector=vector)
                keys = {MOVE_FORWARD:"MOVE_FORWARD",TURN_LEFT:"TURN_LEFT",TURN_RIGHT:"TURN_RIGHT"} # dict keys for convert each num with it command

                if way is None:
                    print("there is no way (you can ignore it)",flush=True)
                    break

                for step in way: # loop to send commands from list ways
                        
                    if not running: # if program takes a SIGTERM , it will off
                        break
                        
                    reply = send(command=step) # send command & get replay
                    reply = reply.split()

                    # handel blocked reply
                    if reply[0] == "BLOCKED": 
                        dct_near_points = { # dict for connect each vector with it point in near points
                            "down" : (now_point[0]+1,now_point[1]), 
                            "right" : (now_point[0],now_point[1]+1), 
                            "up" : (now_point[0]-1,now_point[1]), 
                            "left" : (now_point[0],now_point[1]-1)
                        } 

                        if len(reply) == 1: # check if blocked without cm
                            blocked_point = dct_near_points[vector] # get near blocked point
                            lst_map[blocked_point[0]][blocked_point[1]]= 1 # block near point
                                
                            break # break to recreat a new way with new changes
                            
                        else: # check if blocked within cm (in this case, we will go back as far as the reply's distance allows)
                            send(command=MOVE_BACKWARD,distance=reply[1]) # go back

                            blocked_near_point = dct_near_points[vector] # get near blocked point

                            dct_far_points = { # dict for connect each vector with it point in far points
                                "down" : (blocked_near_point[0]+1,blocked_near_point[1]), 
                                "right" : (blocked_near_point[0],blocked_near_point[1]+1), 
                                "up" : (blocked_near_point[0]-1,blocked_near_point[1]), 
                                "left" : (blocked_near_point[0],blocked_near_point[1]-1)
                            }

                            blocked_far_point = dct_far_points[vector] # get far blocked point
                            lst_map[blocked_near_point[0]][blocked_near_point[1]] = 1 # block near point
                            lst_map[blocked_far_point[0]][blocked_far_point[1]]= 1 # block far point
                            break # break to recreat a new way with new changes

                    
                    now_point,vector = current_point(last_point=now_point,vector=vector,last_command=keys[step])
        else:
            goal_point = None # after arriving at the goal_point , change gaol_points to None

    except Exception as e:
        print(f"error: {e}", file=sys.stderr, flush=True)


# exiting / cleaning here
try:
    save_point(current_point=now_point,vector=vector)
except Exception as e:
    print(f"somethin get wrong when the program write on cache {e}",file=sys.stderr,flush=True)

sys.exit(0)
