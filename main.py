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
SHARE_PATH = "/usr/local/share/jebot"

# general
USER_COMMANDS_FILE = SHARE_PATH + "/user_commands.json"


# motord
MOTORD_CMD_FIFO    = "/run/jebot/motord/cmd"
MOTORD_REPLAY_FIFO = "/run/jebot/motord/reply"


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
MAP_SHARE_FILE = MAP_SHARE_PATH + "map.json"
MAP_CACHE_FILE = CACHE_PATH + "/cash_points.json"

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

        if cmd: return cmd
        else: return None

    def get_current_lang():
        with open(STTD_LANG_FILE, 'r') as f:
            return f.read().strip()
    
    def save_point(current_point:list,goal_point:list,vector:str) -> None: # write current point in MAP_CACHE_FILE
        dct_cache = {
            "cuurrent_point":current_point,
            "goal_point":goal_point,
            "vector":vector
        }
        with open(MAP_CACHE_FILE,"w") as fp:
            json.dump(dct_cache,fp=fp)

    def get_cache() -> dict: # read from cache file
        if not os.path.exists(MAP_CACHE_FILE):
            return None
        
        with open(MAP_CACHE_FILE) as fp:
            return json.load(fp)
    

    """func will send to pipe file ((FIFO) on unix & (based-like)) the command to motord.c """
    def send(command : int,distance=40,angel=90) -> str:
        if os.path.exists(MOTORD_CMD_FIFO) and os.path.exists(MOTORD_REPLAY_FIFO): 
            keys_convertors = {1:f"MOVE_FORWARD {distance} --reply",2:f"TURN_LEFT {angel} --reply",3:f"TURN_RIGHT {angel} --reply",4:f"MOVE_BACKWARD {distance} --replay"} # var contain all command and their num
            with open(MOTORD_CMD_FIFO,"w") as pipe: # open send pipe file
                pipe.write(keys_convertors[command]) # write the command on the pipe
            
            with open(MOTORD_REPLAY_FIFO,"r") as pipe: # open reply pipe file
                replay = pipe.read() # read the reply
                replay.strip() # remove \n from the reply
                return replay
        else:
            print("eroro , pipes not exists.",file=sys.stderr,flush=True)



    current_lang = get_current_lang()

    # load USER_COMMANDS_FILE
    with open(USER_COMMANDS_FILE, "r") as f:
        user_commands = json.load(f)
    
    # read map.json file to get map of the plase & points
    with open(MAP_SHARE_FILE) as fp:
        dct_map = json.load(fp)

    # create paths
    if not os.path.exists(CACHE_PATH):
        os.mkdir(CACHE_PATH, 0o755)
    os.chmod(CACHE_PATH, 0o755)

    if not os.path.exists(VAR_PATH):
        os.mkdir(VAR_PATH, 0o755)
    os.chmod(VAR_PATH, 0o755)


except Exception as e:
    print(f"FATAL ERROR: {e}", file=sys.stderr, flush=True)
    sys.exit(1)


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
            return "error" # if the input is unvalid


# main loop, any exceptions based on 'Exception' here just be print as error log


try: # this try statment it is for cache file (read once & write once)
     # The order of the vars here is important ⚠️ 
     goal_point = None # it vars to decide , if it will take his goal from cache or from user commands
     now_point = None
     vector = None
     dct_cache = get_cache() # get cuurant data from cache    

      #check if tjere is chache file
     if dct_cache:
            vactor = dct_cache["vector"]
            now_point = dct_cache["current_point"]
            goal_point = dct_cache["goal_point"] # if robot walked in a way already this will tell us


except:
    print("there is no cache file (you can ignore it).",file=sys.stderr,flush=True)

while running:
    try:
        # process code here
        # vars
        user_clear_command = get_user_command() # user command
        lst_map = dct_map["maps"]["school_hall"] # get map from map.json

        # if we dont take data from cache file we will use default value
        if not now_point:
            now_point = dct_map["points"]["home"] # default value
        
        if not vector:
            vector = "right"  # default value

        if goal_point:
            while (now_point != goal_point) and running: 
                way,keys = control.command(lst_points=control.path(Map=np.array(lst_map),start_point=now_point,end_point=goal_point),vector=vector)
                                
                if way is None:
                    print("there is no way (you can ignore it)",file=sys.stderr,flush=True)

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
                            send(command=4,distance=reply[1]) # go back

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

            goal_point = None # after arriving at the goal_point , change gaol_points to None
                    
        else:
            if user_clear_command:
                goal_point = user_commands[user_clear_command]["point"]
                if goal_point:
                    goal_point = dct_map["points"][goal_point]

                    while (now_point != goal_point) and running: 
                        way,keys = control.command(lst_points=control.path(Map=np.array(lst_map),start_point=now_point,end_point=goal_point),vector=vector)
                                                
                        if way is None:
                            print("there is no way (you can ignore it)",file=sys.stderr,flush=True)


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
                                    send(command=4,distance=reply[1]) # go back

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
                    
            
            goal_point = None # after arriving at the goal_point , change gaol_points to None 

                            


    except Exception as e:
        print(f"error: {e}", file=sys.stderr, flush=True)

# exiting / cleaning here
try:
    save_point(current_point=now_point,vector=vector,goal_point=goal_point)
except Exception as e:
    print(f"somethin get wrong when the program write on cache {e}",file=sys.stderr,flush=True)

sys.exit(0)
