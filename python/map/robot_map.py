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

import heapq
import numpy as np

"""this func convert Path's (func) output to command for robot , 
it takes Path output as (lst_point) and vector (it is where does robot look) and it return list of commands to robot """
#دالة تحويل النقاط الى اوامر لروبوت
def command(lst_points: list,vector : str) -> list:
    lst_command = [] # متغير تكون فيه كل الاوامر
    first_point = lst_points[0] # اول نقطة في قائمة النقاط
    lst_points = lst_points[1:] # نشيل اول نقطة من قائمة الخطوات
    
    for point in lst_points:
        # متغير النقاط القريبة
        lst_near_points = [(first_point[0]+1,first_point[1]), # down
                           (first_point[0],first_point[1]+1), # right
                           (first_point[0]-1,first_point[1]), # up
                           (first_point[0],first_point[1]-1)] # left

        #قاموس لتسهيل تحديد المتجه
        dct_vectors = {tuple(lst_near_points[0]) : "down",
                       tuple(lst_near_points[1]) : "right",
                       tuple(lst_near_points[2]) : "up",
                       tuple(lst_near_points[3]) : "left"}

        if (vector == "down" and (point == lst_near_points[0])) or (vector == "right" and (point == lst_near_points[1])) or (vector == "up" and (point == lst_near_points[2])) or (vector == "left" and (point == lst_near_points[3])): # move 
            lst_command.append("move 1") # نعطي الامر
            first_point = point # نغير نقطة المرجع الى النقطة الجديدة
        elif (vector == "down" and (point == lst_near_points[3])) or (vector == "right" and (point == lst_near_points[0])) or (vector == "up" and (point == lst_near_points[1])) or (vector == "left" and (point == lst_near_points[2])): # turn right 
            lst_command = lst_command + ["turn right","move 1"] # نعطي الامر
            first_point = point # نغير نقطة المرجع الى النقطة الجديدة
            vector = dct_vectors[point] # تغير المتجه الى المتجه الجديد
        elif (vector == "down" and (point == lst_near_points[1])) or (vector == "right" and (point == lst_near_points[2])) or (vector == "up" and (point == lst_near_points[3])) or (vector == "left" and (point == lst_near_points[0])): # turn left 
            lst_command = lst_command + ["turn left","move 1"] # نعطي الامر
            first_point = point # نغير نقطة المرجع الى النقطة الجديدة
            vector = dct_vectors[point] # تغير المتجه الى المتجه الجديد
        elif (vector == "down" and (point == lst_near_points[2])) or (vector == "right" and (point == lst_near_points[3])) or (vector == "up" and (point == lst_near_points[0])) or (vector == "left" and (point == lst_near_points[1])): # turn left 
            lst_command = lst_command + ["go back","move 1"] # نعطي الامر
            first_point = point # نغير نقطة المرجع الى النقطة الجديدة
            vector = dct_vectors[point] # تغير المتجه الى المتجه الجديد
    
    return lst_command

"""this func responsible for find the shortest way from the begining point to the end point
it takes Map and begining point and end point and return a list points it is like [(1,2),(3,4)]"""
#A*search algorithm
def path(Map : np.array,start_point:tuple,end_point:tuple) -> list:
    #متغيرات
    hight,wight = np.shape(Map) # get map's dimensions
    start_point = tuple(start_point) # ضمان نوع البيانات حيكون صحيح
    end_point = tuple(end_point) # ضمان نوع البيانات حيكون صحيح
    lst_all_points = [] # متغير كل نقطة بس شفناها حتى ما مشينا فيها
    dct_came_from = {} # كل نقطة مشينه فيخا هذه من فين جبناها
    dct_far_start = {start_point : 0}
    set_walked_point = {start_point} # اي نقطة مشينا فيها عشان ما نكررها
    
    #اضافة نقطة البداية الى قائمة النقاط
    heapq.heappush(lst_all_points,(0,start_point))

    # الحلقة الرئيسية
    while lst_all_points:
       far_num , now_point = heapq.heappop(lst_all_points) # المتغير الاول حق وزن النقطة اما الثامي النقطة نفسها
       set_walked_point.add(now_point)

       if now_point == end_point: # هذه وصلنا لنقطة الهدف
            lst_steps = [now_point]
            while now_point != start_point:
                now_point = dct_came_from[now_point]
                lst_steps.append(now_point)
            lst_steps.reverse()
            return lst_steps

       lst_near_point = [[now_point[0]+1,now_point[1]], # متغير النقاط القريبة
                           [now_point[0],now_point[1]+1],
                           [now_point[0]-1,now_point[1]],
                           [now_point[0],now_point[1]-1]]
       # حلقة تمر على النقاط القريبة و تختارافضل شي ممكن يوفي الشروط 
       for point in lst_near_point:
            point = tuple(point)
            
            if hight <= point[0] or wight <= point[1]: # التحقق من اننا ما طلعنا برا الخريطة
                continue

            if point[0] < 0 or point[1] < 0: # التحقق من عدم الطلوع من الخريطة لكن من السالب
                continue

            if Map[point[0]][point[1]] != Map[now_point[0]][now_point[1]]: # التحقق من اننا ما دخلنا في حاجز
                continue

            if point in set_walked_point: # ما نمشي على نفس الخطوة مرتين
                continue

            if not (point in dct_came_from): # عشان نضمن عدم ادخال نفس النقطة مرتين
                dct_came_from[point] = now_point # اضافة النقطة الى شجرة النقاط
                dct_far_start[point] = dct_far_start[now_point]+1 # نحسب عدد الخطوات الي مشيناها من البداية الى الان
                distance = (abs(point[0]-end_point[0]) + abs(point[1]-end_point[1])) + dct_far_start[point] # المسافة بين النقطة الحالية و نقطة النهاية مع عدد الخطوات الي مشيناها
                heapq.heappush(lst_all_points,(distance,point)) # اضافتها الى هيكله البيانات المناسبة

#طريقة استخدام الكود
# way = path(Map,(0,0),(2,2))
# commands = command(way,vector)
