#!/usr/bin/env python3

import sys
# remove python's cache & bytecode
sys.dont_write_bytecode = True

# remove script's directory so `import jebot` finds the package, not this file
# (jebot.py shadows jebot/ otherwise)
del sys.path[0]

import os
import signal
import json
import time
import numpy as np
import jebot.control as control
import jebot.led_patterns as led_patterns


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
STTD_LISTENING_FILE   = "/run/jebot/sttd/listening"
STTD_TEXT_FILE        = "/run/jebot/sttd/text"
STTD_LANG_FILE        = "/run/jebot/sttd/lang"
STTD_LISTEN_MUTE_FILE = "/run/jebot/sttd/listen_mute"

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
    # init vars
    running = True # main loop condition
    child_pid = None
    user_cmd = None
    listening = False
    led_handled = False
    error_led_start_time = 0 # temporary time var until add error sound later

    # signal handler
    def handler(signum, frame):
        global running
        running = False

    signal.signal(signal.SIGINT, handler)
    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGPIPE, signal.SIG_IGN)

    # waiting the daemons to starting (when program run's in boot)
    print("waiting for the daemons...", flush=True)
    time.sleep(3) # 3 seconds to aviod any problems


    def sttd_is_listening():
        with open(STTD_LISTENING_FILE, 'r') as f:
            return bool(int(f.read().strip()))

    # function to get user command from sttd
    def sttd_get_user_command():
        cmd = None

        # temporary fix: check size before read to avoid losing commands
        # written by sttd between the read and clear
        if os.path.getsize(STTD_TEXT_FILE) > 0:
            with open(STTD_TEXT_FILE, 'r') as f:
                cmd = f.read().strip()

            # temporary method to delete the command after reading it (this will be changed later).
            with open(STTD_TEXT_FILE, 'w') as f:
                f.write("")

        return cmd

    def sttd_get_listen_lang():
        with open(STTD_LANG_FILE, 'r') as f:
            return f.read().strip()

    # sttd listening mute control
    def sttd_listen_mute(mute:bool):
        try:
            with open(STTD_LISTEN_MUTE_FILE, 'w') as f:
                f.write(f"{int(mute)}\n")
        except FileNotFoundError:
            pass # ignore it

    listen_lang = sttd_get_listen_lang()
    sounds_path = BASE_SOUNDS_PATH + f"/{listen_lang}"

    # function to play sound from sounds_path using aplay as child process without wait
    def play_sound(sound_name:str, device="default") -> int:
        if not os.path.exists(f"{sounds_path}/{sound_name}"):
            raise FileNotFoundError(f"sound: {sounds_path}/{sound_name} not found")

        sttd_listen_mute(True)

        pid = os.fork()

        if pid == 0:
            null = os.open("/dev/null", os.O_RDWR)

            os.dup2(null, 0)
            os.dup2(null, 1)
            os.dup2(null, 2)

            os.execlp("aplay", "aplay", "-q", "-D", device, f"{sounds_path}/{sound_name}")
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
        keys_convertors = {MOVE_FORWARD:f"MOVE_FORWARD {distance} --reply",TURN_LEFT:f"TURN_LEFT {angel} --reply",TURN_RIGHT:f"TURN_RIGHT {angel} --reply",MOVE_BACKWARD:f"MOVE_BACKWARD {distance} --reply" } # var contain all command and their num
        with open(MOTORD_CMD_FIFO,"w") as pipe: # open send pipe file
            pipe.write(keys_convertors[command]) # write the command on the pipe
        
        with open(MOTORD_REPLY_FIFO,"r") as pipe: # open reply pipe file
            replay = pipe.read() # read the reply
            replay.strip() # remove \n from the reply
            return replay

    def current_point(last_point:list,vector:str,last_command:str) -> tuple:
        # turn_right -> 1
        # turn_left -> -1

        # numbering vectors for use it in algorithm & connect each vector with it new point if last_command was MOVE_FORWARD or MOVE_BACKWARD
        dct_numb_vector = { 
        "up" : [2 , [last_point[0] - 1, last_point[1]]],
        "right" : [3 , [last_point[0], last_point[1]+1]],
        "down" : [0 , [last_point[0] + 1, last_point[1]]],
        "left" : [1 , [last_point[0], last_point[1] - 1]]
        }

        dct_BACKWARD_new_point = { # get new point by use vectors
            "up" : "down",
            "down":"up",
            "right" : "left",
            "left":"right"
        }

        dct_numb_commands = {
            "MOVE_FORWARD" : 0,
            "MOVE_BACKWARD" : 0,
            "TURN_LEFT":-1,
            "TURN_RIGHT":1
        }

        new_vector = dct_numb_vector[vector][0] + dct_numb_commands[last_command]
        new_vector = new_vector if -1 < new_vector < 4 else 3 if new_vector == -1 else 0 # handel unlogical values
        new_point = last_point

        if last_command == "MOVE_FORWARD" or last_command == "MOVE_BACKWARD": 
            new_point = dct_numb_vector[vector][1] if last_command == "MOVE_FORWARD" else dct_numb_vector[dct_BACKWARD_new_point[vector]][1]
        
        return new_point,new_vector


    # load USER_COMMANDS_FILE
    with open(USER_COMMANDS_FILE, "r") as f:
        user_commands = json.load(f)

    # read map.json file to get map of the plase & points
    with open(MAP_SHARE_FILE) as fp:
        dct_map = json.load(fp)
        dct_map["maps"]["school_hall"] = np.array(dct_map["maps"]["school_hall"],dtype=np.int8)

    # create paths
    if not os.path.exists(CACHE_PATH):
        os.mkdir(CACHE_PATH, 0o755)
    os.chmod(CACHE_PATH, 0o755)

    if not os.path.exists(VAR_PATH):
        os.mkdir(VAR_PATH, 0o755)
    os.chmod(VAR_PATH, 0o755)

    # The order of the vars here is important !!! 
    goal_point = None 
    now_point = None
    vector = None
    goal_vector = None # if robot arrive to the goal point , it will adjust its vector to goal vector
    dct_state = get_state() # get cuurant data from cache    

    # check if tjere is chache file
    if dct_state:
        vector = dct_state["vector"]
        now_point = dct_state["current_point"]
    else:
        print("there is no cache file (you can ignore it).",flush=True)

    sttd_listen_mute(False)

except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


# main loop, any exceptions based on 'Exception' here just be print as error log
while running:
    try:
        sounds_path = BASE_SOUNDS_PATH + f"/{listen_lang}"
        lst_map = dct_map["maps"]["school_hall"].copy() # get map from map.json

        try:
            user_cmd = sttd_get_user_command()
        except Exception:
            user_cmd = None
            print("sttd_get_user_command error", flush=True, file=sys.stderr)
        try:
            listen_lang = sttd_get_listen_lang()
        except Exception:
            listen_lang = "en" # default
            print("sttd_get_listen_lang error", flush=True, file=sys.stderr)
        try:
            listening = sttd_is_listening()
        except Exception:
            listening = False
            print("sttd_is_listening error", flush=True, file=sys.stderr)

        # turn on the led if listening
        if listening and not led_handled:
            print("listening", flush=True)
            led_patterns.hearing()
            led_handled = True
            print("led on 'hearing'", flush=True)
        # turn off if handled from listening and not listening
        elif not listening and led_handled and not child_pid and not error_led_start_time:
            print("not listening", flush=True)
            led_patterns.off()
            led_handled = False
            print("led off", flush=True)

        if user_cmd:
            print(f"user command: '{user_cmd}'", flush=True)
            if user_cmd in user_commands:
                sound = user_commands[user_cmd]["sound"]

                if not goal_point:
                    goal_point = user_commands[user_cmd]["point"]
                    if isinstance(goal_point, str):
                        goal_vector = dct_map["points"][goal_point]["vector"]
                        goal_point = dct_map["points"][goal_point]["coordinates"]


                if sound:
                    print(f"sound: {sound}", flush=True)
                    if child_pid:
                        os.kill(child_pid, signal.SIGTERM)
                        os.waitpid(child_pid, 0)
                        child_pid = None

                    child_pid = play_sound(sound)
                    print("mic muted", flush=True)
                    led_patterns.talking()
                    led_handled = True
                    print("led on 'talking'", flush=True)
                else:
                    print("there is no sound", flush=True)

            else:
                print(f"'{user_cmd}' not in user_commands", file=sys.stderr, flush=True)

        # clean child process if finished and close led if it on and unmute the mic
        if child_pid and os.waitpid(child_pid, os.WNOHANG)[0]:
            child_pid = None
            print("sound ended", flush=True)
            sttd_listen_mute(False)
            print("mic unmuted", flush=True)
            if led_handled:
                led_patterns.off()
                led_handled = False
                print("led off", flush=True)

        if error_led_start_time and led_handled and error_led_start_time <= time.time():
            print("led error timeouted, led off", flush=True)
            led_patterns.off()
            error_led_start_time = 0
            led_handled = False

        # if we dont take data from cache file we will use default value
        if not now_point:
            now_point = dct_map["points"]["home"]["coordinates"] # default value
        
        if not vector:
            vector = dct_map["points"]["home"]["vector"]  # default value

        if goal_point and not child_pid: # do not move while speaking
            led_patterns.moving()
            led_handled = True
            print("led on 'moving'", flush=True)
            while goal_point and ((now_point[0] != goal_point[0]) or (now_point[1] != goal_point[1])) and running: 
                way = control.command(lst_points=control.path(Map=lst_map,start_point=now_point,end_point=goal_point),vector=vector)
                keys = {MOVE_FORWARD:"MOVE_FORWARD",TURN_LEFT:"TURN_LEFT",TURN_RIGHT:"TURN_RIGHT"} # dict keys for convert each num with it command

                if way is None:
                    print("there is no way (you can ignore it)",flush=True)
                    break

                command_ptr = 0 # pointer to current command in the robots
                while command_ptr != len(way): # loop to send commands from list ways
                        
                    if not running: # if program takes a SIGTERM , it will off
                        break

                    try:
                        reply = send(command=way[command_ptr]) # send command & get replay
                    except Exception as e:
                        print(f"motord error: {e}", flush=True, file=sys.stderr)
                        goal_point = None
                        # error sound will added later
                        # turn on led red color (error) for 5 seconds
                        led_patterns.error()
                        led_handled = True
                        error_led_start_time = time.time() + 5
                        print("led on 'error'", flush=True)
                        break

                    reply = reply.split()

                    match reply[0]:
                        case "SUCCESS":
                            now_point,vector = current_point(last_point=now_point,vector=vector,last_command=keys[way[command_ptr]])
                            command_ptr += 1

                        case "BLOCKED": # handel blocked reply
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
                        
                        case "FAILED":
                            goal_point = None
                            print(f"motord fatal error: {' '.join(reply)}", flush=True, file=sys.stderr)
                            # error sound will added later

                            led_patterns.error()
                            led_handled = True
                            error_led_start_time = time.time() + 5
                            print("led on 'error'", flush=True)

                            break
            else:
                if goal_point and goal_vector: # check first
                    dct_numb_vector = { #numbering vectors for use it in algorithm
                        "up" : 2,
                        "right" : 3,
                        "down" : 0,
                        "left" : 1
                    }

                    # turn_right -> -1
                    # turn_left -> 1
                    steps = dct_numb_vector[vector] - dct_numb_vector[goal_vector]
                    
                    if steps == 3:
                        steps = -1
                    elif steps == -3:
                        steps = 1
                    
                    for _ in range(abs(steps)):
                        if steps < 0:
                            send(TURN_RIGHT)
                            now_point,vector = current_point(last_point=now_point,vector=vector,last_command="TURN_RIGHT")
                        else:
                            send(TURN_LEFT)
                            now_point,vector = current_point(last_point=now_point,vector=vector,last_command="TURN_LEFT")

                goal_point = None # after arriving at the goal_point , change gaol_points to None
                goal_vector = None

                if not error_led_start_time and led_handled:
                    led_patterns.off()
                    led_handled = False
                    print("led off", flush=True)

        time.sleep(0.01)
    except Exception as e:
        print(f"error: {e}", file=sys.stderr, flush=True)


# exiting / cleaning here
try:
    save_point(current_point=now_point,vector=vector)
    if led_handled:
        led_patterns.off()
except Exception as e:
    print(f"exit error: {e}", file=sys.stderr, flush=True)

sys.exit(0)
