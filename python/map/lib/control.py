#!/usr/bin/env python3
#this is a test map you can use
# 11111111111111111111,
# 11000000000000000101,
# 11111111111111110000,
# 11111111111111110000,
# 11000000000000000111,
# 00011111111111111111,
# 00111111111111111111,
# 10000000000000001111,
# 11111111111111111111,
# 11111111111111111111,

"""Copyright (C) <2026>  <Abdulahk1>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/"""


import heapq
import numpy as np 
import os
import sys



"""this func convert Path's (func) output to command for robot , 
it takes Path output as (lst_point) and vector (it is where does robot look) and it return list of commands to robot """
def command(lst_points: list,vector : str) -> tuple:
    
    if not lst_points: # if lst_points empty , the func will return None 
        return None

    lst_command = [] # lsit has all commands
    first_point = lst_points[0] # save first point in lst_points
    lst_points = lst_points[1:] # remove first point from lst_points
    lst_current_points = []
    keys_convertors = {1:"MOVE_FORWARD",2:"TURN_LEFT",3:"TURN_RIGHT"} # dict for covert string to num , to use less RAM

    for point in lst_points:
        # list near point from our point var
        lst_near_points = [(first_point[0]+1,first_point[1]), # down
                           (first_point[0],first_point[1]+1), # right
                           (first_point[0]-1,first_point[1]), # up
                           (first_point[0],first_point[1]-1)] # left

        #in vector code we collect all situation in one if block so we need way to identify the new vector 
        dct_vectors = {tuple(lst_near_points[0]) : "down",
                       tuple(lst_near_points[1]) : "right",
                       tuple(lst_near_points[2]) : "up",
                       tuple(lst_near_points[3]) : "left"}

        if (vector == "down" and (point == lst_near_points[0])) or (vector == "right" and (point == lst_near_points[1])) or (vector == "up" and (point == lst_near_points[2])) or (vector == "left" and (point == lst_near_points[3])): # move 
            
            lst_command.append(1) # append the command
            first_point = point # change now point to new point
        
        elif (vector == "down" and (point == lst_near_points[3])) or (vector == "right" and (point == lst_near_points[0])) or (vector == "up" and (point == lst_near_points[1])) or (vector == "left" and (point == lst_near_points[2])): # turn right 
            
            lst_command = lst_command + [3,1] # append the commands
            # robot dont change it place when turn right , that is why i append now point with the next point
            first_point = point 
            vector = dct_vectors[point] 
        
        elif (vector == "down" and (point == lst_near_points[1])) or (vector == "right" and (point == lst_near_points[2])) or (vector == "up" and (point == lst_near_points[3])) or (vector == "left" and (point == lst_near_points[0])): # turn left 
            
            lst_command = lst_command + [2,1] # append the commands
            # robot dont change it place when turn right , that is why i append now point with the next point
            first_point = point 
            vector = dct_vectors[point] 
        
        elif (vector == "down" and (point == lst_near_points[2])) or (vector == "right" and (point == lst_near_points[3])) or (vector == "up" and (point == lst_near_points[0])) or (vector == "left" and (point == lst_near_points[1])): # go back
            
            lst_command = lst_command + [3,3,2] # append the commands
            # robot dont change it place when turn right , that is why i append now point with the next point
            first_point = point 
            vector = dct_vectors[point]
    
    return np.array(lst_command,dtype=np.int8),keys_convertors

"""this func responsible for find the shortest way from the begining point to the end point
it takes Map and begining point and end point and return a list points it is like [(1,2),(3,4)]"""
#A*search algorithm
def path(Map : np.array,start_point:tuple,end_point:tuple) -> list:
    #vars
    hight,wight = np.shape(Map) # get map's dimensions
    start_point = tuple(start_point) # make sure that the type of var is correct
    end_point = tuple(end_point) # make sure that the type of var is correct
    lst_all_points = [] # any point program had seen it will store here even we dont waik on it
    dct_came_from = {} # a tree have all point we have walked on & thair parents
    dct_far_start = {start_point : 0}
    set_walked_point = {start_point} # any point we have walked on it will store here 
    
    # append beginning point
    heapq.heappush(lst_all_points,(0,start_point))

    #  maon loop
    while lst_all_points:
       far_num , now_point = heapq.heappop(lst_all_points) # pull point from list & the first var for wighof point and 2st for the point itself
       set_walked_point.add(now_point)

       if now_point == end_point: # if we arrive to goal point
            lst_steps = [now_point]
            while now_point != start_point:
                now_point = dct_came_from[now_point]
                lst_steps.append(now_point)
            lst_steps.reverse()
            return lst_steps

       lst_near_point = [[now_point[0]+1,now_point[1]], # var near points
                           [now_point[0],now_point[1]+1],
                           [now_point[0]-1,now_point[1]],
                           [now_point[0],now_point[1]-1]]
       # a loop walk in the nesr points & choose the best one 
       for point in lst_near_point:
            point = tuple(point)
            
            if hight <= point[0] or wight <= point[1]: # check if we are in map or no
                continue

            if point[0] < 0 or point[1] < 0: # check if we are in map or no
                continue

            if Map[point[0]][point[1]] == 1: # check if there is obstacle or no
                continue

            if point in set_walked_point: # ما نمشي على نفس الخطوة مرتين
                continue

            if not (point in dct_came_from): # to check we will not add same point twice
                dct_came_from[point] = now_point # add point to tree points
                dct_far_start[point] = dct_far_start[now_point]+1 # count the number of steps we have walked from beginning to now
                distance = (abs(point[0]-end_point[0]) + abs(point[1]-end_point[1])) + dct_far_start[point] # The distance between the current point and the endpoint, along with the number of steps taken.
                heapq.heappush(lst_all_points,(distance,point)) # add vars to the tree


# code usage (just to try the code)
################################ 
# strr = """11111111111111111111
# 11000000000000000101
# 11111111111111110000
# 11111111111111110000
# 11000000000000000111
# 00011111111111111111
# 00111111111111111111
# 10000000000000001111
# 11111111111111111111
# 11111111111111111111"""
# Map = [list(map(int, line)) for line in strr.splitlines()] # new_map for big trying
# way = path(Map,(1,2),(7,15))
############################
# Map = np.zeros((3,3))
# way = path(Map,(0,0),(2,2))
###############################
# print(way)
# commands,keys = command(way,"down")
# print(commands)

# Map shape
# [
#   [0,0,0],
#   [0,0,0],
#   [0,0,0]
# ]

