#include "../include/timer.h"
#include "../include/runtime.h"

#include <lgpio.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>


/* usage */
#define USAGE "usage:\n\t--trig <trig_gpio>\n\t--echo <echo_gpio>"

/* motord pid file */
#define MOTORD_PIDFILE RUNTIME_PATH "/motord/pid"

/* config */
#define MAX_DISTANCE 30         // CM
#define WAIT_TIME_US 750000     // 0.75 seconds in microseconds

/* global vars */
volatile bool running = true;   // loop condition
bool blocked = false;           // if sended signal to motord
int gpio = -1;                  // gpio handler (lgpio)

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
    float distance;
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
    duration = pulsein(echo, 10000); // 10000 for testing (~= 150cm)

    distance = duration * 0.0343 / 2.0;

    return distance; // CM
}

int main(int argc, char *argv[]) {

    int trig = -1, echo = -1;

    /* get trig & echo gpio number */
    if (argc-1 < 4) {
        fprintf(stderr, "missing args.\n");
        fprintf(stderr, "%s\n", USAGE);
        return 1;
    }
    else {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--trig") == 0) {
                trig = atoi(argv[i+1]);
            }
            else if (strcmp(argv[i], "--echo") == 0) {
                echo = atoi(argv[i+1]);
            }
        }
    }

    if ((trig < 0 || echo < 0) || trig == echo) {
        fprintf(stderr, "invalid args.\n");
        return 1;
    }

    /* get motord pid */
    pid_t motord_pid;
    FILE *motord_pid_file = fopen(MOTORD_PIDFILE, "r");
    if (!motord_pid_file) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m fopen %s: %s\n", MOTORD_PIDFILE, strerror(errno));
        return 1;
    }
    fscanf(motord_pid_file, "%d", &motord_pid);
    fclose(motord_pid_file);
    
    /* try to send signal to motord */
    if (kill(motord_pid, 0) < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m kill %d: %s\n", motord_pid, strerror(errno));
        return 1;
    }

    gpio = lgGpiochipOpen(0);
    if (gpio < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m lgGpiochipOpen: %s\n", lguErrorText(gpio));
        return 1;
    }

    /* signal handling */
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

    if (gpio < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m lgGpiochipOpen: %s\n", lguErrorText(gpio));
        return 1;
    }


    /* setup lines */
    int io;
    if (
        (io = lgGpioClaimOutput(gpio, LG_SET_OUTPUT, trig, 0)) < 0 ||
        (io = lgGpioClaimInput(gpio, LG_SET_PULL_NONE, echo)) < 0
    ) {
        fprintf(stderr, "Error: %s\n", lguErrorText(io));
        return 1;
    }


    /* vars */
    float distance = 0, first_distance = 0;
    uint64_t wait_time = 0;
    bool waiting = false;
    

    printf("PID: %d\n", getpid());
    printf("Motord PID: %d\n", motord_pid);

    puts("\n|********** Ultrasonicd Is Started **********|\n");

    while(running) {
        distance = get_distance(trig, echo);

        uint64_t now = timer_now(TIMER_US);

        if (distance) { // if not timeouted

            /* debug / verbose */
            // printf("distance:       %.2fcm\n", distance);
            // printf("first_distance: %.2fcm\n", first_distance);

            if (distance <= MAX_DISTANCE) {
                if (!waiting) {
                    /* start timer */
                    first_distance = distance;
                    wait_time = now;
                    waiting = true;
                }
                else {
                    if ((now - wait_time) >= WAIT_TIME_US) { // if timer timeout
                        if (first_distance >= distance) {
                            if (!blocked) {
                                printf("Obstacle Detected: %.2fcm\n", distance);
                                kill(motord_pid, SIGUSR1);
                                blocked = true;
                            }
                        }
                        waiting = false;
                    }
                }
            }
            else {
                first_distance = 0;
                waiting = false;

                if (blocked) {
                    kill(motord_pid, SIGUSR2);
                    blocked = false;
                }
            }
        }
        else {
            first_distance = 0;
            waiting = false;

            if (blocked) {
                kill(motord_pid, SIGUSR2);
                blocked = false;
            }
        }

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
    
    lgGpiochipClose(gpio);

    kill(motord_pid, SIGUSR2); // unblock motord before exit

    return 0;
}
