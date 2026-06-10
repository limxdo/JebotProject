#include "../include/timer.h"
#include "../include/runtime.h"
#include "../include/logger.h"

#include <lgpio.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* motord pid file */
#define MOTORD_PIDFILE RUNTIME_PATH "/motord/pid"

/* config (tmp) */
#define MAX_DISTANCE_CM 25

/* GPIOs */
#define RIGHT_ECHO 27
#define RIGHT_TRIG 17

#define LEFT_ECHO 25
#define LEFT_TRIG 22

/* global vars */
int gpio = -1;                  // gpio handler (lgpio)
volatile bool running = true;   // loop condition
bool blocked = false;           // if sended signal to motord
pid_t motord_pid = -1;

typedef struct {
    int trig;
    int echo;

    float distance_cm;

    char dist_file_path[64];

    pthread_t thread_id;
    pthread_mutex_t lock;
} ultrasonic_t;

/* signal handler */
void handler(int signum) {
    running = false; // stop the loop
}


/*
 * Ultrasonic timing design notes:
 *
 * pulsein(echo, timeout_us):
 *   This function blocks while waiting for the echo pulse.
 *   It first waits for the signal to go HIGH, then measures how
 *   long it stays HIGH. If either phase exceeds timeout_us,
 *   it returns 0 (no object detected or out of range).
 *
 *   timeout_us therefore:
 *     - Limits the maximum measurable distance.
 *     - Limits worst-case blocking time when no object is present.
 *
 *   Max distance (cm) ~= (timeout_us * 0.0343) / 2
 *
 *   With 50000 us:
 *     Max measurable distance ~= 8.5 meters (sensor-limited in practice).
 *
 * Main loop delay (usleep(<US>)):
 *   This delay prevents overlapping ultrasonic waves and
 *   reduces CPU usage. It does NOT affect distance calculation,
 *   only the measurement rate.
 *
 * Important when scaling:
 *   - timeout_us should be chosen based on the maximum distance
 *     you actually need to measure (not the sensor's theoretical max).
 *   - Too large timeout -> slower response when no object exists
 *     due to longer blocking time.
 *   - Too small timeout -> distant objects will not be detected.
 *   - When using multiple ultrasonic sensors, trigger them sequentially.
 *     Simultaneous triggering may cause acoustic cross-talk and
 *     incorrect measurements.
 */
uint64_t pulsein(int echo, uint64_t timeout_us) {

    uint64_t start, end, start_time;

    start_time = timer_now(TIMER_US);
    while (lgGpioRead(gpio, echo) == 0) {
        if (timer_now(TIMER_US) - start_time > timeout_us) return 0; // timeouted
    }
    start = timer_now(TIMER_US);

    start_time = timer_now(TIMER_US);
    while (lgGpioRead(gpio, echo) == 1) {
        if (timer_now(TIMER_US) - start_time > timeout_us) return 0; // timeouted
    }
    end = timer_now(TIMER_US);

    return end - start;
}

/* get distance in cm */
float get_distance(int trig, int echo) {
    uint64_t duration;

    lgGpioWrite(gpio, trig, 0);
    timer_busy_wait(TIMER_US, 2);
    lgGpioWrite(gpio, trig, 1);
    timer_busy_wait(TIMER_US, 10);
    lgGpioWrite(gpio, trig, 0);

    //lgTxPulse(gpio, trig, 10, 0, 0, 1);

     /*
      * can get timeout_us for any distance (cm) from:
      * (2 * distance) / 0.0343
      */
    duration = pulsein(echo, 10000); // 10000 (tmp) for (~= 150cm)

    return (duration * 0.0343f / 2.0f); // CM
}

/* thread func */
void* ultrasonic_thread_func(void *arg) {
    ultrasonic_t *ultrasonic = arg;

    while (running) {

        /* update distance */
        pthread_mutex_lock(&ultrasonic->lock);
        ultrasonic->distance_cm = get_distance(ultrasonic->trig, ultrasonic->echo);
        pthread_mutex_unlock(&ultrasonic->lock);

        usleep(50000);
    }

    pthread_mutex_lock(&ultrasonic->lock);
    ultrasonic->distance_cm = 0.0f;
    pthread_mutex_unlock(&ultrasonic->lock);

    return NULL;
}

int main(void) {

    int exit_status = 0;

    if (runtime_init("ultrasonicd", 0755) < 0) {
        log_fatal("Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* get motord pid */
    FILE *motord_pid_file = fopen(MOTORD_PIDFILE, "r");
    if (motord_pid_file) {
        fscanf(motord_pid_file, "%d", &motord_pid);
        fclose(motord_pid_file);
    }
    else {
        motord_pid = -1;
    }

    gpio = lgGpiochipOpen(0);
    if (gpio < 0) {
        log_fatal("lgGpiochipOpen: %s\n", lguErrorText(gpio));
        return 1;
    }

    /* signal handling */
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

    if (gpio < 0) {
        log_fatal("lgGpiochipOpen: %s\n", lguErrorText(gpio));
        return 1;
    }

    /* setup lines */
    int io = -1;

    if ((io = lgGpioClaimOutput(gpio, LG_SET_OUTPUT, RIGHT_TRIG, 0)) < 0) {
        log_fatal("%s\n", lguErrorText(io));
        exit_status = 1;
        goto exit;
    }
    if ((io = lgGpioClaimInput(gpio, LG_SET_PULL_NONE, RIGHT_ECHO)) < 0) {
        log_fatal("%s\n", lguErrorText(io));
        exit_status = 1;
        goto exit;
    }

    if ((io = lgGpioClaimOutput(gpio, LG_SET_OUTPUT, LEFT_TRIG, 0)) < 0) {
        log_fatal("%s\n", lguErrorText(io));
        exit_status = 1;
        goto exit;
    }
    if ((io = lgGpioClaimInput(gpio, LG_SET_PULL_NONE, LEFT_ECHO)) < 0) {
        log_fatal("%s\n", lguErrorText(io));
        exit_status = 1;
        goto exit;
    }


    /* ultrasonics */
    ultrasonic_t front_right = {
        .trig = RIGHT_TRIG,
        .echo = RIGHT_ECHO,
        .dist_file_path = "front_right",
        0
    };
    ultrasonic_t front_left = {
        .trig = LEFT_TRIG,
        .echo = LEFT_ECHO,
        .dist_file_path = "front_left",
        0
    };

    pthread_create(&front_right.thread_id, NULL, ultrasonic_thread_func, &front_right);
    pthread_create(&front_left.thread_id, NULL, ultrasonic_thread_func, &front_left);

    /* vars to copy distance from threads */
    float right_distance, left_distance;

    while(running) {

        /* update motord pid */
        motord_pid_file = fopen(MOTORD_PIDFILE, "r");
        if (motord_pid_file) {
            fscanf(motord_pid_file, "%d", &motord_pid);
            fclose(motord_pid_file);
        } else {
            motord_pid = -1;
        }


        /* get distances from threads */
        pthread_mutex_lock(&front_right.lock);
        right_distance = front_right.distance_cm;
        pthread_mutex_unlock(&front_right.lock);

        pthread_mutex_lock(&front_left.lock);
        left_distance = front_left.distance_cm;
        pthread_mutex_unlock(&front_left.lock);

        if (motord_pid > 0) {
            if (right_distance || left_distance) {
                if (right_distance <= MAX_DISTANCE_CM || left_distance <= MAX_DISTANCE_CM) {
                    if (!blocked) {
                        kill(motord_pid, SIGUSR1);
                        blocked = true;
                    }
                }
                else {
                    if (blocked) {
                        kill(motord_pid, SIGUSR2);
                        blocked = false;
                    }
                }
            }
            else {
                if (blocked) {
                    kill(motord_pid, SIGUSR2);
                    blocked = false;
                }
            }
        }

        /* write distances */
        runtime_write_atomic(front_right.dist_file_path, 0644, "%.2f\n", right_distance);
        runtime_write_atomic(front_left.dist_file_path, 0644, "%.2f\n", left_distance);

        /*
         * Main loop delay (in microseconds).
         *
         * Loop frequency (Hz) ~= 1,000,000 / delay_us
         *
         * Example:
         *   usleep(50000)  -> 1,000,000 / 50,000  = 20 Hz  (~20 measurements/sec)
         *   usleep(20000)  -> 1,000,000 / 20,000  = 50 Hz
         *   usleep(100000) -> 1,000,000 / 100,000 = 10 Hz
         *
         * Note:
         *   Actual frequency will be slightly lower due to execution time
         *   of distance measurement and other processing inside the loop.
         */
        usleep(50000);
    }
    
exit:
    pthread_join(front_right.thread_id, NULL);
    pthread_join(front_left.thread_id, NULL);

    lgGpiochipClose(gpio);

    runtime_exit();

    kill(motord_pid, SIGUSR2); // unblock motord before exit

    return exit_status;
}
