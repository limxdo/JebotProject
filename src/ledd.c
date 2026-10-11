#include "../include/runtime.h"
#include "../include/logger.h"
#include "../include/gpio_chardev.h"

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* runtime */
#define LEDD_RUNTIME_PATH RUNTIME_PATH "/ledd"
#define LEDD_CMD_FIFO LEDD_RUNTIME_PATH "/cmd"

/* gpio */
#define R_GPIO 9
#define G_GPIO 10
#define B_GPIO 11

/*
 * NOTE: current LEDs is temporary common anode
 */

/* pwm */
#define PWM_FREQ 1000

gpio_chip_t chip0;
volatile bool running = true;

void handler(int signum) {
    running = false;
}

/* store RGB (0-255) values */
struct rgb_led {
    int R,G,B;
};

int parse_request(struct rgb_led *rgb_led, char *request) {
    rgb_led->R = -1;
    rgb_led->G = -1;
    rgb_led->B = -1;
    if (!request) return 0;

    char *argptr;

    /* get R */
    argptr = strtok(request, " ");
    if (!argptr) return -1;
    rgb_led->R = atoi(argptr);
    if (rgb_led->R < 0 || rgb_led->R > 255) {
        parse_request(rgb_led, NULL);
        return -1;
    }

    /* get G */
    argptr = strtok(NULL, " ");
    if (!argptr) return -1;
    rgb_led->G = atoi(argptr);
    if (rgb_led->G < 0 || rgb_led->G > 255) {
        parse_request(rgb_led, NULL);
        return -1;
    }

    /* get B */
    argptr = strtok(NULL, " ");
    if (!argptr) return -1;
    rgb_led->B = atoi(argptr);
    if (rgb_led->B < 0 || rgb_led->B > 255) {
        parse_request(rgb_led, NULL);
        return -1;
    }

    return 0;
}

int main(void) {

    /* signal handling */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGPIPE, SIG_IGN);

    /* init vars */
    int exit_status = 0;
    int cmd_fifo_fd = -1;
    
    int runtime_init_ret = -1;
    int gpio_chip_open_ret = -1;

    if ((runtime_init_ret = runtime_init("ledd", 0755)) < 0) {
        log_fatal("Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* open gpiochip */
    if ((gpio_chip_open_ret = gpio_chip_open(&chip0, 0, "ledd")) < 0) {
        log_fatal("gpio_chip_open: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* creating LEDD_CMD_FIFO */
    if (runtime_fifo("cmd", 0622) < 0) {
        log_fatal("runtime_fifo %s: %s\n", LEDD_CMD_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* open LEDD_CMD_FIFO fd with Non Blocking */
    cmd_fifo_fd = open(LEDD_CMD_FIFO, O_RDONLY | O_NONBLOCK);
    if (cmd_fifo_fd < 0) {
        log_fatal("open cmd_fifo_fd: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* claim output with `GPIOHANDLE_REQUEST_ACTIVE_LOW` for common anode LEDs */
    if (
        gpio_claim_output(&chip0, R_GPIO, GPIOHANDLE_REQUEST_ACTIVE_LOW, 0) < 0 ||
        gpio_claim_output(&chip0, G_GPIO, GPIOHANDLE_REQUEST_ACTIVE_LOW, 0) < 0 ||
        gpio_claim_output(&chip0, B_GPIO, GPIOHANDLE_REQUEST_ACTIVE_LOW, 0) < 0
    ) {
        log_fatal("gpio_claim_output: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }


    struct rgb_led rgb_led;
    ssize_t rn = -1;
    char request[32];

    while (running) {
        /* read fifo */
        rn = read(cmd_fifo_fd, request, sizeof(request)-1);
        if (rn > 0) {
            if (request[rn-1] == '\n') request[rn-1] = '\0';
            else request[rn] = '\0';

            log_print("\n");
            log_info("RECEIVED COMMAND: %s\n", request);

            if (parse_request(&rgb_led, request) < 0) {
                log_err("INVALID COMMAND\n");
            }
            else {
                log_debug("R: %d\n", rgb_led.R);
                log_debug("G: %d\n", rgb_led.G);
                log_debug("B: %d\n", rgb_led.B);

                /* start PWM for LED */
                if (gpio_software_pwm(&chip0, R_GPIO, PWM_FREQ, (rgb_led.R / 255.0f) * 100.0f) < 0) {
                    log_err("PWM FAILED ON R_GPIO (%d): %s\n", R_GPIO, strerror(errno));
                }
                    
                if (gpio_software_pwm(&chip0, G_GPIO, PWM_FREQ, (rgb_led.G / 255.0f) * 100.0f) < 0) {
                    log_err("PWM FAILED ON G_GPIO (%d): %s\n", G_GPIO, strerror(errno));
                }

                if (gpio_software_pwm(&chip0, B_GPIO, PWM_FREQ, (rgb_led.B / 255.0f) * 100.0f) < 0) {
                    log_err("PWM FAILED ON B_GPIO (%d): %s\n", B_GPIO, strerror(errno));
                }
            }
        }
        usleep(1);
    }

exit:

    if (cmd_fifo_fd >= 0)
        close(cmd_fifo_fd);

    if (gpio_chip_open_ret >= 0)
        gpio_chip_close(&chip0);

    if (runtime_init_ret >= 0)
        runtime_exit();

    return exit_status;
}
