#include <pigpiod_if.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


/* GPIOs */
#define ECHO    23
#define TRIG    24

/* files */
#define MOTORD_PIDFILE "/tmp/motord.pid"

/* config (can change it) */
#define MAX_DISTANCE 30         // CM
#define WAIT_TIME_US 750000     // 0.75 seconds in microseconds

/* global vars */
volatile bool RUNNING = true;   // loop condition
bool blocked = false;           // if sended signal to motord

/* signal handler */
void handler(int signum) {
    RUNNING = false; // stop the loop
}


/*
 * Ultrasonic timing design notes:
 *
 * pulsein(echo, timeout_us):
 *   This function blocks while waiting for the ECHO pulse.
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
 *   With 50000 µs:
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
 *   - Too large timeout → slower response when no object exists
 *     due to longer blocking time.
 *   - Too small timeout → distant objects will not be detected.
 *   - When using multiple ultrasonic sensors, trigger them sequentially.
 *     Simultaneous triggering may cause acoustic cross-talk and
 *     incorrect measurements.
 */
uint32_t pulsein(int echo, unsigned long timeout_us) {

    uint32_t start_time = get_current_tick();
    uint32_t start, end;

    while (gpio_read(echo) == 0) {
        if (get_current_tick() - start_time > timeout_us) return 0; // timeouted
    }
    start = get_current_tick();

    start_time = get_current_tick();
    while (gpio_read(echo) == 1) {
        if (get_current_tick() - start_time > timeout_us) return 0; // timeouted
    }
    end = get_current_tick();

    return end - start;
}

/* get distance in cm */
float get_distance(int trig, int echo) {
    float distance;
    uint32_t duration;

    gpio_write(trig, 0);
    usleep(2);
    gpio_write(trig, 1);
    usleep(10);
    gpio_write(trig, 0);

     /*
      * can get timeout_us for any distance from:
      * (2 * distance) / 0.0343
      */
    duration = pulsein(echo, 10000); // 10000 for testing (~= 150cm)

    distance = duration * 0.0343 / 2.0;

    return distance; // CM
}

int main(void) {

    printf("PID: %d\n", getpid());

    /* get motord pid */
    pid_t motord_pid;
    FILE *motord_pid_f = fopen(MOTORD_PIDFILE, "r");
    if (!motord_pid_f) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m fopen %s: %s\n", MOTORD_PIDFILE, strerror(errno));
        return 1;
    }
    fscanf(motord_pid_f, "%d", &motord_pid);
    fclose(motord_pid_f);
    
    /* try to send signal to motord */
    if (kill(motord_pid, 0) < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m kill %d: %s\n", motord_pid, strerror(errno));
        return 1;
    }

    printf("Motord PID: %d\n", motord_pid);

    int conn = pigpio_start(NULL, NULL);

    /* signal handling */
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

    if (conn < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m pigpio_start: %s\n", pigpio_error(conn));
        return 1;
    }


    /* setup lines */
    set_mode(TRIG , 1); // output
    set_mode(ECHO , 0); // input


    /* vars */
    float distance = 0, first_distance = 0;
    uint32_t wait_time = 0;
    bool waiting = false;
    

    puts("\n|********** Ultrasonicd Is Started **********|\n");

    while(RUNNING) {
        distance = get_distance(TRIG, ECHO);

        uint32_t now = get_current_tick();

        if (distance) { // if not timeouted

            /* debug / verbose */
            //printf("distance:       %.2fcm\n", distance);
            //printf("first_distance: %.2fcm\n", first_distance);

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
    
    pigpio_stop();

    kill(motord_pid, SIGUSR2); // unblock motord before exit

    puts("\nexiting...");

    return 0;
}
