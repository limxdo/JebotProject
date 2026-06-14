#include "../include/runtime.h"
#include "../include/pwm_sysfs.h"
#include "../include/logger.h"
#include "../include/timer.h"

#include <lgpio.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* runtime paths */
#define MOTORD_RUNTIME_PATH RUNTIME_PATH "/motord"

#define MOTORD_CMD_FIFO   MOTORD_RUNTIME_PATH "/cmd"
#define MOTORD_REPLY_FIFO MOTORD_RUNTIME_PATH "/reply"


/* PWMs */
#define RIGHT_RPWM pwm0 // pwm channel 0 -> GPIO 12
#define RIGHT_LPWM pwm1 // pwm channel 1 -> GPIO 13
#define LEFT_RPWM  pwm2 // pwm channel 2 -> GPIO 18
#define LEFT_LPWM  pwm3 // pwm channel 3 -> GPIO 19

#define RIGHT_FORWARD  RIGHT_RPWM
#define RIGHT_BACKWARD RIGHT_LPWM
#define LEFT_FORWARD   LEFT_RPWM
#define LEFT_BACKWARD  LEFT_LPWM

/* PWM config */
#define PERIOD_HZ          10000 // 10kHz
#define DUTY_CYCLE_PERCENT 35


/* Encoders */
#define RIGHT_ENCODER_A_GPIO 23
#define LEFT_ENCODER_A_GPIO  24

/* Empirically calibrated: 20 pulses/cm (based on multiple runs over 10cm, 25cm, 40cm, 100cm, 200cm). May vary with chassis or ground conditions. */
#define PULSES_PER_CM 20

uint64_t left_enc_a_counter = 0;
uint64_t right_enc_a_counter = 0;
uint64_t target_pulses = 0;


/* MPU6050 (Gyro) */
#define I2C_BUS "/dev/i2c-1"

#define MPU6050_ADDR 0x68

#define MPU6050_PWR_MGMT_1  0x6B
#define MPU6050_GYRO_CONFIG 0x1B
#define MPU6050_GYRO_ZOUT_H 0x47
#define MPU6050_GYRO_ZOUT_L 0x48
#define GYRO_CONFIG 0x00 // -/+250 deg/s

/* Empirically calibrated 61 bais for gyro_z based on 15*1000 measurements */
#define GYRO_BAIS_Z 61

#define MPU6050_WAKEUP 0x00
#define MPU6050_SLEEP  0x40

float yaw = 0.0f;
long target_angle = 0;

/* global vars */
volatile bool running = true;   // main loop
volatile bool blocked = false;  // if ultrasonicd detect an obstacle, send signal to motord to block any FORWARD request
int gpio = -1;                  // gpio handler (lgpio)

/* PWMs */
pwm_t pwm0 = PWM_INIT,
      pwm1 = PWM_INIT,
      pwm2 = PWM_INIT,
      pwm3 = PWM_INIT;


/* Encoders interrupt service routine function */
void encoder_isr(int e, lgGpioAlert_p evt, void *userdata) {
    for (int i = 0; i < e; i++) {
        if (evt[i].report.gpio == RIGHT_ENCODER_A_GPIO) {
            right_enc_a_counter++;
        }
        else if (evt[i].report.gpio == LEFT_ENCODER_A_GPIO) {
            left_enc_a_counter++;
        }
    }
}


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
    long distance_cm;
    long angle;
    bool reply;
};

/* function to filter a struct from one string */
void parse_cmd(struct request_args *request_args, char *request) {

    /* default values */
    request_args->cmd = CMD_UNKNOWN;
    request_args->distance_cm = 0;
    request_args->angle = 0;
    request_args->reply = false;
    if (request == NULL) // uses to set default values in struct & exit 
        return;

    char *argptr; // strtok
    char *endptr; // strtol

    /* first argument */
    argptr = strtok(request, " ");

    /* get type of request_args->cmd */
    if (!argptr) return; // if empty

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

    /* second argument */
    argptr = strtok(NULL, " ");
    
    if (!argptr) return;
    switch (request_args->cmd) {
    case CMD_MOVE_FORWARD:
    case CMD_MOVE_BACKWARD:
        request_args->distance_cm = strtol(argptr, &endptr, 10);
        if (endptr == argptr || *endptr == '-') // not a number or negative number
            request_args->distance_cm = 0;
        break;

    case CMD_TURN_RIGHT:
    case CMD_TURN_LEFT:
        request_args->angle = strtol(argptr, &endptr, 10);
        if (endptr == argptr || *endptr == '-') // not a number or negative number
            request_args->angle = 0;
        break;
    default:
        break;
    }

    argptr = strtok(NULL, " ");
    if (!argptr) return;
    else if (strcmp(argptr, "--reply") == 0) {
        request_args->reply = true;
    }
}

/* function to moving */
int move(cmd_t cmd, ...) {

    /*
     * BTS7960 motor drivers connected to hardware PWM (sysfs).
     * Two motors per driver (left/right sides).
     * R_EN and L_EN are tied to 3.3V (always enabled).
     * Control via RPWM (forward) and LPWM (backward).
     * Encoder feedback implemented (distance & straight line).
     * Gyro not yet integrated (planned for tank turn).
     */

    va_list args;
    va_start(args, cmd);

    /* reset values */
    right_enc_a_counter = 0;
    left_enc_a_counter = 0;
    target_pulses = 0;
    target_angle = 0;
    yaw = 0.0f;

    int status = 0;

    /* execute command and error checking */
    switch (cmd) {
    case CMD_MOVE_FORWARD:
        /* convert distance_cm to encoder pulses */
        target_pulses = va_arg(args, long) * PULSES_PER_CM;

        /* check if pwm failed */
        if (
            ((status = pwm_enable(&RIGHT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACKWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_BACKWARD, false)) < 0)
        ) {
            move(CMD_STOP); // try to stop motors
        }

        break;

    case CMD_MOVE_BACKWARD:
        target_pulses = va_arg(args, long) * PULSES_PER_CM;

        if (
            ((status = pwm_enable(&RIGHT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACKWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_BACKWARD, true)) < 0)
        ) {
            move(CMD_STOP);
        }

        break;

    case CMD_TURN_RIGHT:
            target_angle = va_arg(args, long);
        if (
            ((status = pwm_enable(&RIGHT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACKWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&LEFT_BACKWARD, false)) < 0)
        ) {
            move(CMD_STOP);
        }

        break;

    case CMD_TURN_LEFT:
            target_angle = va_arg(args, long);
        if (
            ((status = pwm_enable(&RIGHT_FORWARD, true)) < 0) ||
            ((status = pwm_enable(&RIGHT_BACKWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_FORWARD, false)) < 0) ||
            ((status = pwm_enable(&LEFT_BACKWARD, true)) < 0)
        ) {
            move(CMD_STOP);
        }

        break;

    case CMD_STOP:
        /* do not call move(CMD_STOP, 0) again to avoid recursion if pwm failed */
        status = pwm_enable(&RIGHT_FORWARD, false);
        status = pwm_enable(&RIGHT_BACKWARD, false);
        status = pwm_enable(&LEFT_FORWARD, false);
        status = pwm_enable(&LEFT_BACKWARD, false);

        right_enc_a_counter = 0;
        left_enc_a_counter = 0;
        target_pulses = 0;
        target_angle = 0;
        break;
    
    default:
        status = -1;
    }

    va_end(args);
    return status;
}

/* reply function */
void reply(reply_t reply, long moved) {

    int total_wait_us = 20000;

    int reply_fifo_fd = -1;
    char msg[32];

    switch (reply) {
    case REPLY_SUCCESS:
        strcpy(msg, "SUCCESS\n");
        break;
    case REPLY_FAILED:
        strcpy(msg, "FAILED\n");
        break;
    case REPLY_SKIPPED:
        if (moved)
            snprintf(msg, sizeof(msg), "SKIPPED %ld\n", moved);
        else
            strcpy(msg, "SKIPPED\n");
        break;
    case REPLY_BLOCKED:
        if (moved)
            snprintf(msg, sizeof(msg), "BLOCKED %ld\n", moved);
        else
            strcpy(msg, "BLOCKED\n");
        break;
    default:
        return;
    }

    int tries = 4;
    /* open MOTORD_REPLY_FIFO with tries */
    while ((reply_fifo_fd = open(MOTORD_REPLY_FIFO, O_WRONLY | O_NONBLOCK)) < 0 && tries-- > 0) {
        usleep(total_wait_us / tries);
    }
    if (reply_fifo_fd < 0) {
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

    
    /* setup encoders */
    lgGpioClaimInput(gpio, LG_SET_PULL_NONE, RIGHT_ENCODER_A_GPIO);
    lgGpioClaimInput(gpio, LG_SET_PULL_NONE, LEFT_ENCODER_A_GPIO);

    lgGpioSetAlertsFunc(gpio, RIGHT_ENCODER_A_GPIO, encoder_isr, NULL);
    lgGpioSetAlertsFunc(gpio, LEFT_ENCODER_A_GPIO, encoder_isr, NULL);
    lgGpioClaimAlert(gpio, 0, LG_BOTH_EDGES, RIGHT_ENCODER_A_GPIO, -1);
    lgGpioClaimAlert(gpio, 0, LG_BOTH_EDGES, LEFT_ENCODER_A_GPIO, -1);

    /* Open PWMs */
    if (
        pwm_open(&pwm0, 0, 0) < 0 ||
        pwm_open(&pwm1, 0, 1) < 0 ||
        pwm_open(&pwm2, 0, 2) < 0 ||
        pwm_open(&pwm3, 0, 3) < 0
    ) {
        log_fatal("pwm_open: %s\n", strerror(errno));
        exit_status = 1;
        goto exit;
    }

    pwm_set_period_hz(&RIGHT_FORWARD, PERIOD_HZ);
    pwm_set_period_hz(&RIGHT_BACKWARD, PERIOD_HZ);
    pwm_set_period_hz(&LEFT_FORWARD, PERIOD_HZ);
    pwm_set_period_hz(&LEFT_BACKWARD, PERIOD_HZ);

    pwm_set_duty_cycle_percentage(&RIGHT_FORWARD, DUTY_CYCLE_PERCENT);
    pwm_set_duty_cycle_percentage(&RIGHT_BACKWARD, DUTY_CYCLE_PERCENT);
    pwm_set_duty_cycle_percentage(&LEFT_FORWARD, DUTY_CYCLE_PERCENT);
    pwm_set_duty_cycle_percentage(&LEFT_BACKWARD, DUTY_CYCLE_PERCENT);

    /* creating MOTORD_CMD_FIFO */
    if (runtime_fifo("cmd", 0622) < 0) {
        log_fatal("runtime_fifo %s: %s\n", MOTORD_CMD_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* open fd for MOTORD_CMD_FIFO */
    int cmd_fifo_fd = -1;
    if ((cmd_fifo_fd = open(MOTORD_CMD_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
        log_fatal("open %s: %s\n", MOTORD_CMD_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* creating MOTORD_REPLY_FIFO */
    if (runtime_fifo("reply", 0644) < 0) {
        log_fatal("runtime_fifo %s: %s\n", MOTORD_REPLY_FIFO, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    /* Gyro */
    int mpu6050_fd = open(I2C_BUS, O_RDWR);
    if (mpu6050_fd < 0) {
        log_fatal("open %s: %s\n", I2C_BUS, strerror(errno));
        exit_status = 1;
        goto exit;
    }

    if (ioctl(mpu6050_fd, I2C_SLAVE, MPU6050_ADDR) < 0) {
        log_fatal("ioctl 0x%x: %s\n", MPU6050_ADDR, strerror(errno));
        exit_status = 1;
        goto exit;
    }


    /* Starting the daemon */
    char request[256];
    ssize_t rn;
    long moved;

    int move_ret = -1;
    struct request_args request_args;
    parse_cmd(&request_args, NULL);

    uint8_t wdata[2], rdata[2];

    /* wakeup the MPU6050 */
    wdata[0] = MPU6050_PWR_MGMT_1;
    wdata[1] = MPU6050_WAKEUP;
    write(mpu6050_fd, wdata, 2);

    wdata[0] = MPU6050_GYRO_CONFIG;
    wdata[1] = GYRO_CONFIG;
    write(mpu6050_fd, wdata, 2);

    /* gyro_z */
    float dt = 0.0f;
    float dps_z;
    uint8_t reg = MPU6050_GYRO_ZOUT_H;
    int16_t gyro_z_raw;
    uint64_t last_time = timer_now(TIMER_MS);

    while (running) {
        uint64_t now = timer_now(TIMER_MS);
        dt = (now - last_time) / 1000.0f;
        last_time = now;

        /* get gyro_z */
        write(mpu6050_fd, &reg, 1);
        read(mpu6050_fd, rdata, 2);
        gyro_z_raw = (rdata[0] << 8) | rdata[1];
        dps_z = (gyro_z_raw - GYRO_BAIS_Z) / 131.0f;
        yaw += fabsf(dps_z * dt);

        /* if exists command, check if command finished */
        // if (target_pulses && ((right_enc_a_counter + left_enc_a_counter) / 2 >= target_pulses)) {
        if (target_pulses && (right_enc_a_counter >= target_pulses && left_enc_a_counter >= target_pulses)) {
            move(CMD_STOP);
            log_info("EXECUTION: SUCCESS\n");
            /* if need to reply */
            if (request_args.reply)
                reply(REPLY_SUCCESS, 0);
            /* reset values */
            parse_cmd(&request_args, NULL);
        }
        else if (request_args.cmd == CMD_MOVE_FORWARD && blocked) { // block the command if already executing
            moved = ((right_enc_a_counter + left_enc_a_counter) / 2 / PULSES_PER_CM);
            move(CMD_STOP);
            log_info("EXECUTION: BLOCKED (%ld cm moved)\n", moved);
            if (request_args.reply)
                reply(REPLY_BLOCKED, moved);
            /* reset values to avoid print loop */
            parse_cmd(&request_args, NULL);
        }
        else if (target_angle && (yaw >= target_angle)) {
            move(CMD_STOP);
            log_info("EXECUTION: SUCCESS\n");
            if (request_args.reply)
                reply(REPLY_SUCCESS, 0);
            parse_cmd(&request_args, NULL);
        }

        rn = read(cmd_fifo_fd, request, sizeof(request)-1); // -1 to add '\0'
        if (rn > 0) {
            if (request[rn-1] == '\n') request[rn-1] = '\0';
            else request[rn] = '\0';

            if (target_pulses || target_angle) {
                if (target_pulses) {
                    moved = ((right_enc_a_counter + left_enc_a_counter) / 2 / PULSES_PER_CM);
                    log_info("EXECUTION: SKIPPED (%ld cm moved)\n", moved);
                    if (request_args.reply)
                        reply(REPLY_SKIPPED, moved);
                    move(CMD_STOP);
                }
                else if (target_angle){
                    moved = yaw;
                    log_info("EXECUTION: SKIPPED (%ld angle turned)\n", moved);
                    if (request_args.reply)
                        reply(REPLY_SKIPPED, moved);
                    move(CMD_STOP);
                }
                else {
                    log_info("EXECUTION: SKIPPED\n");
                    if (request_args.reply)
                        reply(REPLY_SKIPPED, 0);
                    move(CMD_STOP);
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
            else if (
                ((request_args.cmd == CMD_MOVE_FORWARD || request_args.cmd == CMD_MOVE_BACKWARD) && !request_args.distance_cm) ||
                ((request_args.cmd == CMD_TURN_RIGHT || request_args.cmd == CMD_TURN_LEFT) && !request_args.angle)
            ) {
                log_err("MISSING ARGS\n");
                parse_cmd(&request_args, NULL);
            }
            else if (request_args.cmd == CMD_MOVE_FORWARD && blocked) { // block command before executing
                log_info("EXECUTION: BLOCKED\n");
                if (request_args.reply) {
                    reply(REPLY_BLOCKED, 0);
                }
                parse_cmd(&request_args, NULL);
            }
            else {
                if (request_args.cmd == CMD_MOVE_FORWARD || request_args.cmd == CMD_MOVE_BACKWARD)
                    move_ret = move(request_args.cmd, request_args.distance_cm);
                else if (request_args.cmd == CMD_TURN_RIGHT || request_args.cmd == CMD_TURN_LEFT)
                    move_ret = move(request_args.cmd, request_args.angle);
                else
                    move_ret = move(request_args.cmd, -1);

                if (move_ret < 0) {
                    log_err("EXECUTION: FAILED (%s)\n", errno ? strerror(errno) : lguErrorText(move_ret));
                    move(CMD_STOP);
                    if (request_args.reply)
                        reply(REPLY_FAILED, 0);
                }
                else if (request_args.cmd == CMD_STOP) {
                    log_info("EXECUTION: SUCCESS\n");
                    if (request_args.reply)
                        reply(REPLY_SUCCESS, 0);
                }
            }
        }

        usleep(1);
    }

exit:
    /* stop motors */
    move(CMD_STOP);

    /* close gpiochip */
    lgGpiochipClose(gpio);

    /* close PWMs */
    pwm_close(&pwm0);
    pwm_close(&pwm1);
    pwm_close(&pwm2);
    pwm_close(&pwm3);

    /* sleep the MPU6050 */
    wdata[0] = MPU6050_PWR_MGMT_1;
    wdata[1] = MPU6050_SLEEP;
    write(mpu6050_fd, wdata, 2);

    close(mpu6050_fd);
    close(cmd_fifo_fd);

    runtime_exit();

    return exit_status;
}
