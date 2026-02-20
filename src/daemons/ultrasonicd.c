#include <pigpiod_if.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>


/* GPIOs */
#define RIGHT_MOTORS   17
#define LEFT_MOTORS    27
#define ECHO           23
#define TRIG           24

/* robot-cmd file path */
#define CMD_FIFO "/tmp/robot-cmd"

/* config (can change it) */
#define MAX_DISTANCE 20 // CM
#define WAIT_TIME_US (0.75 * 1000000)
bool RUNNING = true;    // loop condition

/* signal handler */
void handler(int signum) {
    RUNNING = false;    // stop the loop
    puts("");
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
    duration = pulsein(echo, 10000); // for testing (~= 150cm)

    distance = duration * 0.0343 / 2.0;

    return distance; // CM
}

int main(void) {

    printf("PID: %d\n\n", getpid());

    int conn = pigpio_start(NULL, NULL);

    /* signal handling */
    signal(SIGTERM, handler);
    signal(SIGINT, handler);
    signal(SIGPIPE, SIG_IGN);

    /* setup lines */
    set_mode(TRIG , 1); // output
    set_mode(ECHO , 0); // input


    /* open fd for CMD_FIFO */
    int CMD_FIFO_FD = -1;
    while ((CMD_FIFO_FD = open(CMD_FIFO, O_WRONLY)) < 0) {
        perror("\033[33mWARNING: open CMD_FIFO_FD\033[0m");
        usleep(10000);
    }

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
                if ((gpio_read(RIGHT_MOTORS) || gpio_read(LEFT_MOTORS))) { // temporary condition for test
                    if (!waiting) {
                        /* start timer */
                        first_distance = distance;
                        wait_time = now;
                        waiting = true;
                    }
                    else {
                        if ((now - wait_time) >= WAIT_TIME_US) { // if timer timeout
                            if (first_distance >= distance) {
                                printf("Object Detected: %.2fcm\n", distance);
                                write(CMD_FIFO_FD, "STOP", sizeof("STOP")); // send STOP to motord to stop motors
                            }

                            waiting = false;
                        }
                    }
                }
            }
            else {
                first_distance = 0;
                waiting = false;
            }
        }
        else {
            first_distance = 0;
            waiting = false;
        }

        usleep(50000); // main loop delay
    }
    
    pigpio_stop();

    if (CMD_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    puts("\nexiting...");

    return 0;
}

