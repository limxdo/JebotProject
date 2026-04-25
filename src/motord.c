#include "../include/runtime.h"
#include "../include/pwm_sysfs.h"
#include "../include/timer.h"
#include "../include/logger.h"

#include <lgpio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* runtime paths */
#define MOTORD_RUNTIME_PATH RUNTIME_PATH "/motord"

#define MOTORD_CMD_FIFO   MOTORD_RUNTIME_PATH "/cmd"
#define MOTORD_REPLY_FIFO MOTORD_RUNTIME_PATH "/reply"


/* PWMs */
#define RIGHT_RPWM    pwm0
#define RIGHT_LPWM    pwm1
#define LEFT_RPWM     pwm2
#define LEFT_LPWM     pwm3

#define RIGHT_FORWARD RIGHT_RPWM
#define RIGHT_BACK    RIGHT_LPWM
#define LEFT_FORWARD  LEFT_RPWM
#define LEFT_BACK     LEFT_LPWM

/* PWM config */
#define PERIOD     PWM_PERIOD_MAX
#define DUTY_CYCLE PERIOD


/* global vars */
volatile bool running = true;   // main loop
volatile bool blocked = false;  // if ultrasonicd detect an obstacle, send signal to motord to block any FORWARD request
bool do_reply = false;          // know if reply
int move_ret = -1;              // move() return
int gpio = -1;                  // gpio handler (lgpio)
uint64_t cmd_timer = 0;         // timer of command

pwm_t pwm0 = PWM_INIT,
      pwm1 = PWM_INIT,
      pwm2 = PWM_INIT,
      pwm3 = PWM_INIT;


/* handler of signals */
void handler(int signum) {
    if (signum == SIGUSR1) {
        blocked = true;
    }
    else if (signum == SIGUSR2) {
        blocked = false;
    }
    else if (signum == SIGINT || signum == SIGTERM) {
        running = false; // stop the loop
    }
}

/* command types */
typedef enum {
    CMD_MOVE_FORWARD,
    CMD_MOVE_BACKWARD,
    CMD_TURN_RIGHT,
    CMD_TURN_LEFT,
    CMD_STOP,
    CMD_UNKNOWN
} cmd_t;

/* reply types */
typedef enum {
    REPLY_SUCCESS,
    REPLY_FAILED,
    REPLY_SKIPPED,
    REPLY_BLOCKED
} reply_t;

/* struct to store command & args from MOTORD_CMD_FIFO */
struct request_args {
    cmd_t cmd;
    unsigned long seconds;
    bool reply;
};
/* NOTE: 'seconds' is temporary. Will be replaced with centimeters (cm)
 * once encoders are properly calibrated and integrated.
 */

/* function to filter a struct from one string */
void parse_cmd(struct request_args *request_args, char *request) {

    /* default values */
    request_args->cmd = CMD_UNKNOWN;
    request_args->seconds = 0;
    request_args->reply = false;
    if (request == NULL) // uses to set default values in struct & exit 
        return;

    char *argptr; // strtok
    char *endptr; // strtoul

    /* first argument */
    argptr = strtok(request, " ");

    /* get type of request_args->cmd */
    if (argptr == NULL) { // if empty
        return;
    }
    else {
        /* place values with ENUMs */
        if (strcmp(argptr, "MOVE_FORWARD") == 0) {
            request_args->cmd = CMD_MOVE_FORWARD;
        }
        else if (strcmp(argptr, "MOVE_BACKWARD") == 0) {
            request_args->cmd = CMD_MOVE_BACKWARD;
        }
        else if (strcmp(argptr, "TURN_RIGHT") == 0) {
            request_args->cmd = CMD_TURN_RIGHT;
        }
        else if (strcmp(argptr, "TURN_LEFT") == 0) {
            request_args->cmd = CMD_TURN_LEFT;
        }
        else if (strcmp(argptr, "STOP") == 0) {
            request_args->cmd = CMD_STOP;
        }
        else {
            request_args->cmd = CMD_UNKNOWN;
            return;
        }
    }

    /* second argument */
    argptr = strtok(NULL, " ");
    
    if (argptr == NULL) {
        return;
    }
    else {
        /* if arg2 is reply */
        if (strcmp(argptr, "--reply") == 0) {
            request_args->reply = true;
            return;
        }
        else {
            /* if seconds */
            request_args->seconds = strtoul(argptr, &endptr, 10);
            if (endptr == argptr || *endptr == '-') {  // not a number or negative number
                request_args->seconds = 0;
            }
        }
    }

    /* third argument */
    argptr = strtok(NULL, " ");

    if (argptr == NULL) {
        return;
    }
    else {
        if (strcmp(argptr, "--reply") == 0) {
            request_args->reply = true;
        }
    }
}

/* main function to moving */
int move(cmd_t cmd, unsigned long seconds) {

    /*
     * BTS7960 motor drivers connected to hardware PWM (sysfs).
     * Two motors per driver (left/right sides).
     * R_EN and L_EN are tied to 3.3V (always enabled).
     * Control via RPWM (forward) and LPWM (backward).
     * Encoder and gyro feedback not yet implemented.
     */

    /* convert sec to ms */
    seconds *= 1000;
    int status;

    unsigned long angle_90 = 850; // ms, will be replaced with gyro feedback

    /* execute command and error checking */
    switch (cmd) {
    case CMD_MOVE_FORWARD:
        pwm_set_duty_cycle(&RIGHT_FORWARD, DUTY_CYCLE);
        pwm_set_duty_cycle(&LEFT_FORWARD, DUTY_CYCLE);

        /* check if writing pwm */
        if (
            ((status = pwm_enable(&RIGHT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACK, false)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_BACK, false)) < 0)
        ) {
            move(CMD_STOP, 0); // try to stop motors
            return status;
        }

        /* start timer if success */
        cmd_timer = timer_now(TIMER_MS) + seconds;
        break;

    case CMD_MOVE_BACKWARD:
        pwm_set_duty_cycle(&RIGHT_BACK, DUTY_CYCLE);
        pwm_set_duty_cycle(&LEFT_BACK, DUTY_CYCLE);

        if (
            ((status = pwm_enable(&RIGHT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACK, true)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_BACK, true)) < 0)
        ) {
            move(CMD_STOP, 0);
            return status;
        }

        cmd_timer = timer_now(TIMER_MS) + seconds;
        break;

    case CMD_TURN_RIGHT:
        pwm_set_duty_cycle(&RIGHT_BACK, PERIOD);
        pwm_set_duty_cycle(&LEFT_FORWARD, PERIOD);

        if (
            ((status = pwm_enable(&RIGHT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACK, true)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_BACK, false)) < 0)
        ) {
            move(CMD_STOP, 0);
            return status;
        }

        cmd_timer = timer_now(TIMER_MS) + angle_90;
        break;

    case CMD_TURN_LEFT:
        pwm_set_duty_cycle(&RIGHT_FORWARD, PERIOD);
        pwm_set_duty_cycle(&LEFT_BACK, PERIOD);

        if (
            ((status = pwm_enable(&RIGHT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACK, false)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_BACK, true)) < 0)
        ) {
            move(CMD_STOP, 0);
            return status;
        }

        cmd_timer = timer_now(TIMER_MS) + angle_90;
        break;

    case CMD_STOP:
        pwm_set_duty_cycle(&RIGHT_FORWARD, 0);
        pwm_set_duty_cycle(&RIGHT_BACK, 0);
        pwm_set_duty_cycle(&LEFT_FORWARD, 0);
        pwm_set_duty_cycle(&LEFT_BACK, 0);

        /* do not call move(CMD_STOP, 0) again to avoid recursion if pwm failed */
        if (
            ((status = pwm_enable(&RIGHT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACK, false)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_BACK, false)) < 0)
        )
            return status;

        cmd_timer = 0;
        break;
    
    default:
        return -1;
    }

    return 0; // return true
}

/* reply function */
void reply(reply_t reply) {

    usleep(20000); // delay before replying

    int reply_fifo_fd = -1;
    char msg[16];

    switch (reply) {
    case REPLY_SUCCESS:
        strcpy(msg, "SUCCESS\n");
        break;
    case REPLY_FAILED:
        strcpy(msg, "FAILED\n");
        break;
    case REPLY_SKIPPED:
        strcpy(msg, "SKIPPED\n");
        break;
    case REPLY_BLOCKED:
        strcpy(msg, "BLOCKED\n");
        break;
    default:
        return;
    }

    /* open MOTORD_REPLY_FIFO */
    if ((reply_fifo_fd = open(MOTORD_REPLY_FIFO, O_WRONLY | O_NONBLOCK)) < 0) {
        log_err("REPLY: ERROR (%s)\n", strerror(errno));
        return;
    }
    else {
        if (write(reply_fifo_fd, msg, strlen(msg)) < 0) {
            log_err("REPLY: WRITE ERROR (%s)\n", strerror(errno));
        } else {
            log_info("REPLY: SUCCESS\n");
        }

        close(reply_fifo_fd);
    }
}

int main(void) {

    /* exit signals */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);

    /* blocking handlers */
    signal(SIGUSR1, handler);
    signal(SIGUSR2, handler);

    /* ignore signals */
    signal(SIGPIPE, SIG_IGN);

    int exit_status = 0;

    if (runtime_init("motord", 0755) < 0) {
        log_fatal("Runtime failed: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }
    runtime_pid(getpid());

    /* open gpiochip */
    gpio = lgGpiochipOpen(0);

    if (gpio < 0) {
        log_fatal("lgGpiochipOpen: %s\n", lguErrorText(gpio));
        exit_status = 1;
        goto exit;
    }

    /* Open PWMs */
    if (
        pwm_open(&pwm0, 0, 0) < 0 ||
        pwm_open(&pwm1, 0, 1) < 0 ||
        pwm_open(&pwm2, 0, 2) < 0 ||
        pwm_open(&pwm3, 0, 3) < 0
    ) {
        log_fatal("FATAL ERROR: pwm_open: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    pwm_set_period(&pwm0, PERIOD);
    pwm_set_period(&pwm1, PERIOD);
    pwm_set_period(&pwm2, PERIOD);
    pwm_set_period(&pwm3, PERIOD);

    pwm_set_duty_cycle(&pwm0, DUTY_CYCLE);
    pwm_set_duty_cycle(&pwm1, DUTY_CYCLE);
    pwm_set_duty_cycle(&pwm2, DUTY_CYCLE);
    pwm_set_duty_cycle(&pwm3, DUTY_CYCLE);

    /* creating MOTORD_CMD_FIFO file */
    unlink(MOTORD_CMD_FIFO); // if exists
    if (mkfifo(MOTORD_CMD_FIFO, 0622) < 0) {
        log_fatal("mkfifo %s: %s\n", MOTORD_CMD_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }
    chmod(MOTORD_CMD_FIFO, 0622);

    /* open fd for MOTORD_CMD_FIFO */
    int cmd_fifo_fd = -1;
    if ((cmd_fifo_fd = open(MOTORD_CMD_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
        log_fatal("open %s: %s\n", MOTORD_CMD_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    unlink(MOTORD_REPLY_FIFO);
    if (mkfifo(MOTORD_REPLY_FIFO, 0644) < 0) {
        log_fatal("mkfifo %s: %s\n", MOTORD_REPLY_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }
    chmod(MOTORD_REPLY_FIFO, 0644);


    /* Starting the daemon */
    char request[256];
    ssize_t rn;

    struct request_args request_args;
    parse_cmd(&request_args, NULL);


    log_info("PID: %d\n", getpid());
    log_info("|********** Motord Is Started **********|\n");
    while(running) {

        /* if exists command, check if command finished */
        if (cmd_timer && timer_now(TIMER_MS) >= cmd_timer) {
            move(CMD_STOP, 0);
            log_info("EXECUTION: SUCCESS\n");
            /* if need to reply */
            if (do_reply) {
                do_reply = false;
                reply(REPLY_SUCCESS);
                move_ret = -1;
            }
            /* reset values */
            parse_cmd(&request_args, NULL);
        }
        else if (request_args.cmd == CMD_MOVE_FORWARD && blocked) { // block the command if already executing
            log_info("EXECUTION: BLOCKED\n");
            move(CMD_STOP, 0);
            if (do_reply) {
                reply(REPLY_BLOCKED);
                do_reply = false;
            }
            /* reset values to avoid print loop */
            parse_cmd(&request_args, NULL);
        }

        rn = read(cmd_fifo_fd, request, sizeof(request)-1); // -1 to add '\0'
        if (rn > 0) {
            if (request[rn-1] == '\n') request[rn-1] = '\0';
            else request[rn] = '\0';

            if (cmd_timer) {
                log_info("EXECUTION: SKIPPED (%.2f seconds left)\n", (cmd_timer - timer_now(TIMER_MS)) / 1000.0 /* convert to seconds (float) */ );
                if (do_reply) {
                    move(CMD_STOP, 0);
                    reply(REPLY_SKIPPED);
                    do_reply = false;
                }
            }

            log_print("\n");
            log_info("RECEIVED COMMAND: '%s'\n", request);

            parse_cmd(&request_args, request);

            /* checking */
            if (request_args.cmd == CMD_UNKNOWN) {
                log_err("INVALID COMMAND\n");
                /* reset values */
                parse_cmd(&request_args, NULL);
            }
            else if ((request_args.cmd == CMD_MOVE_FORWARD || request_args.cmd == CMD_MOVE_BACKWARD) && request_args.seconds <= 0) {
                log_err("MISSING SECONDS\n");
                parse_cmd(&request_args, NULL);
            }
            else if ((request_args.cmd == CMD_TURN_RIGHT || request_args.cmd == CMD_TURN_LEFT) && request_args.seconds) {
                log_err("INVALID OPTIONS\n");
                parse_cmd(&request_args, NULL);
            }
            else if (request_args.cmd == CMD_MOVE_FORWARD && blocked) { // block command before executing
                log_info("EXECUTION: BLOCKED\n");
                if (request_args.reply) {
                    reply(REPLY_BLOCKED);
                }
                parse_cmd(&request_args, NULL);
            }
            else {
                move_ret = move(request_args.cmd, request_args.seconds);

                if (move_ret < 0) {
                    if (errno) {
                        log_err("EXECUTION: FAILED (%s)\n", strerror(errno));
                        move(CMD_STOP, 0);
                        if (request_args.reply)
                            reply(REPLY_FAILED);
                        move_ret = -1;
                    }
                    else {
                        log_err("EXECUTION: FAILED (%s)\n", lguErrorText(move_ret));
                        if (request_args.reply)
                            reply(REPLY_FAILED);
                        move_ret = -1;
                    }
                }
                else if (request_args.cmd == CMD_STOP) {
                    if (move_ret < 0)
                        log_err("EXECUTION: FAILED\n");
                    else
                        log_info("EXECUTION: SUCCESS\n");
                    if (request_args.reply)
                        reply(move_ret < 0 ? REPLY_FAILED : REPLY_SUCCESS);
                }
                else if (request_args.reply) {
                    do_reply = true;
                }
                else {
                    do_reply = false;
                    move_ret = -1;
                }
            }
        }

        usleep(1);
    }

exit:
    /* stop motors */
    move(CMD_STOP, 0);

    /* close gpiochip */
    lgGpiochipClose(gpio);

    /* close PWMs */
    pwm_close(&pwm0);
    pwm_close(&pwm1);
    pwm_close(&pwm2);
    pwm_close(&pwm3);

    close(cmd_fifo_fd);
    unlink(MOTORD_CMD_FIFO);
    unlink(MOTORD_REPLY_FIFO);

    runtime_exit();

    return exit_status;
}
