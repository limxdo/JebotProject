#include <pigpiod_if.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

/* files */
#define CMD_FIFO    "/tmp/motord-cmd"
#define REPLY_FIFO  "/tmp/motord-reply"
#define PIDFILE     "/tmp/motord.pid"


/* GPIOs */
#define  RIGHT_MOTORS   17
#define  LEFT_MOTORS    27


/* global vars */
volatile bool RUNNING = true;   // main loop
volatile bool blocked = false;  // if ultrasonicd detect an obstacle, send signal to motord to block any FORWARD request
bool do_reply = false;          // know if reply
int move_ret = -1;              // move() return
uint64_t cmd_timer = 0;         // timer of command


/* handler of signals */
void handler(int signum) {
    if (signum == SIGUSR1) {
        blocked = true;
    }
    else if (signum == SIGUSR2) {
        blocked = false;
    }
    else if (signum == SIGINT || signum == SIGTERM) {
        RUNNING = false; // stop the loop
    }
}

/* timer in ms */
uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    /*          convert sec to ms                  convert ns to ms */
    return ((uint64_t)ts.tv_sec * 1000ULL) + ((uint64_t)ts.tv_nsec / 1000000ULL);
}


/* enum command types */
enum cmd_type {
    CMD_MOVE_FORWARD,
    CMD_MOVE_BACK,
    CMD_TURN_RIGHT,
    CMD_TURN_LEFT,
    CMD_STOP,
    CMD_UNKNOWN
};

/* enum reply types */
enum reply_type {
    REPLY_SUCCESS,
    REPLY_FAILED,
    REPLY_SKIPPED,
    REPLY_BLOCKED
};

/* struct to store command & args from CMD_FIFO */
struct cmd_args {
    enum cmd_type type;
    long seconds;
    bool reply;
};
/* NOTE: 'seconds' in struct currently represents the duration for which the motors run.
 * Once the wheels are available and we know their exact diameter and the motor speed,
 * this value can be converted to a more meaningful unit (e.g., centimeters traveled, 
 * or any other distance metric). For now, we keep it in seconds as a temporary placeholder.
 */

/* function to filter a struct from one string */
void parse_cmd(struct cmd_args *request_args, char *request) {

    /* default values */
    request_args->type = CMD_UNKNOWN;
    request_args->seconds = 0;
    request_args->reply = false;
    if (request == NULL) // uses to set default values in struct & exit 
        return;

    char *argptr; // strtok
    char *endptr; // strtol

    /* first argument */
    argptr = strtok(request, " ");

    /* get type of request_args->type */
    if (argptr == NULL) { // if empty
        return;
    }
    else {
        /* place values with ENUMs */
        if (strcmp(argptr, "MOVE_FORWARD") == 0) {
            request_args->type = CMD_MOVE_FORWARD;
        }
        else if (strcmp(argptr, "MOVE_BACK") == 0) {
            request_args->type = CMD_MOVE_BACK;
        }
        else if (strcmp(argptr, "TURN_RIGHT") == 0) {
            request_args->type = CMD_TURN_RIGHT;
        }
        else if (strcmp(argptr, "TURN_LEFT") == 0) {
            request_args->type = CMD_TURN_LEFT;
        }
        else if (strcmp(argptr, "STOP") == 0) {
            request_args->type = CMD_STOP;
        }
        else {
            request_args->type = CMD_UNKNOWN;
            return;
        }
    }

    /* second argument */
    argptr = strtok(NULL, " ");
    
    if (argptr == NULL) {
        return;
    }
    else {
        /* if second is reply */
        if (strcmp(argptr, "--reply") == 0) {
            request_args->reply = true;
            return;
        }
        else {
            /* if seconds */
            request_args->seconds = strtol(argptr, &endptr, 10);
            if (endptr == argptr) {  // not a number
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
int move(enum cmd_type type, long seconds) {

    /* 
     * NOTE: All GPIO operations in this function are currently temporary and for testing purposes only.
     * The actual motors / motor drivers are not available yet.
     * Currently, each GPIO controls two wheels (so 4 wheels total are split over 2 GPIOs).
     * This setup can be easily updated once the real motors / motor drivers are integrated.
     */

    /* convert sec to ms */
    seconds *= 1000;
    int gpio_write_status;

    unsigned int angle_90 = 2 * 1000; // 2 seconds (tmp) for example

    /* execute command and error checking */
    switch (type) {
        case CMD_MOVE_FORWARD:
            /* check if writing gpio success of failed */
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 1)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 1)) < 0)
            ) {
                move(CMD_STOP, 0); // try to stop motors
                return gpio_write_status;
            }

            /* start timer if success */
            cmd_timer = now_ms() + seconds;
            break;

        case CMD_MOVE_BACK:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 1)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 1)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timer = now_ms() + seconds;
            break;

        case CMD_TURN_RIGHT:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 1)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 0)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timer = now_ms() + angle_90;
            break;

        case CMD_TURN_LEFT:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 0)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 1)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timer = now_ms() + angle_90;
            break;

        case CMD_STOP:
            /* do not call move(CMD_STOP, 0) again to avoid recursion if pigpiod failed */
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 0)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 0)) < 0)
            )
                return gpio_write_status;

            cmd_timer = 0;
            break;
    }

    return 1; // return true
}

/* reply function */
void reply(enum reply_type type) {

    usleep(20000); // delay before replying

    int REPLY_FIFO_FD = -1;
    char msg[16];

    switch (type) {
        case REPLY_SUCCESS:
            strcpy(msg, "SUCCESS");
            break;
        case REPLY_FAILED:
            strcpy(msg, "FAILED");
            break;
        case REPLY_SKIPPED:
            strcpy(msg, "SKIPPED");
            break;
        case REPLY_BLOCKED:
            strcpy(msg, "BLOCKED");
            break;
    }

    /* open REPLY_FIFO */
    if ((REPLY_FIFO_FD = open(REPLY_FIFO, O_WRONLY | O_NONBLOCK)) < 0) {
        fprintf(stderr, "REPLY: \033[31mERROR\033[0m (%s)\n", strerror(errno));
        return;
    }
    else {
        if (write(REPLY_FIFO_FD, msg, strlen(msg)) < 0) {
            fprintf(stderr, "REPLY: \033[31mWRITE ERROR\033[0m (%s)\n", strerror(errno));
        } else {
            printf("REPLY: \033[32mSUCCESS\033[0m\n");
        }

        close(REPLY_FIFO_FD);
    }
}

int main(void) {

    /* write PID to PIDFILE */
    FILE *pid_f = fopen(PIDFILE, "w");
    if (!pid_f) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m fopen %s: %s\n", PIDFILE, strerror(errno));
        return 1;
    }
    /* write PID */
    fprintf(pid_f, "%d", getpid());
    fclose(pid_f);


    printf("PID: %d\n", getpid());

    /* connect to pigpiod */
    int conn = pigpio_start(NULL, NULL); // (NULL, NULL) uses to connect to local gpio daemon (pigpiod)

    /* exit handlers */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGPIPE, SIG_IGN);

    /* blocking handlers */
    signal(SIGUSR1, handler);
    signal(SIGUSR2, handler);

    if (conn < 0) {
        fprintf(stderr, "\033[31mFATAL ERROR:\033[0m pigpio_start: %s\n", pigpio_error(conn));
        return 1;
    }

    /*
     * 1 -> OUTPUT
     * 0 -> INPUT
     */
    set_mode(LEFT_MOTORS, 1);
    set_mode(RIGHT_MOTORS, 1);

    /* robot-cmd FD */
    int CMD_FIFO_FD = -1;

    /* creating CMD_FIFO file */
    while (mkfifo(CMD_FIFO, 0666) < 0) {
        fprintf(stderr, "\033[33mWARNING:\033[0m mkfifo %s: %s\n", CMD_FIFO, strerror(errno));
        unlink(CMD_FIFO);
        usleep(10000) ;
    }
    chmod(CMD_FIFO, 0622);

    /* open fd for CMD_FIFO */
    while ((CMD_FIFO_FD = open(CMD_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
        fprintf(stderr, "\033[33mWARNING:\033[0m open %s: %s", CMD_FIFO, strerror(errno));
        usleep(10000);
    }

    while (mkfifo(REPLY_FIFO, 0666) < 0) {
        fprintf(stderr, "\033[33mWARNING:\033[0m mkfifo %s: %s\n", REPLY_FIFO, strerror(errno));
        unlink(REPLY_FIFO);
        usleep(10000) ;
    }
    chmod(REPLY_FIFO, 0644);

    /*******************************************************/

    char request[256];
    ssize_t rn;

    struct cmd_args request_args;
    parse_cmd(&request_args, NULL);

    puts("\n|********** Motord Is Started **********|\n");
    while(RUNNING) {

        /* if exists command, check if command finished */
        if (cmd_timer && now_ms() >= cmd_timer) {
            move(CMD_STOP, 0);
            printf("EXECUTION: \033[32mSUCCESS\033[0m\n");
            /* if need to reply */
            if (do_reply) {
                do_reply = false;
                reply(REPLY_SUCCESS);
                move_ret = -1;
            }
            /* reset values */
            parse_cmd(&request_args, NULL);
        }
        else if (request_args.type == CMD_MOVE_FORWARD && blocked) { // block the command if already executing
            printf("EXECUTION: \033[33mBLOCKED\033[0m\n");
            move(CMD_STOP, 0);
            if (do_reply) {
                reply(REPLY_BLOCKED);
                do_reply = false;
            }
            /* reset values to avoid print loop */
            parse_cmd(&request_args, NULL);
        }

        rn = read(CMD_FIFO_FD, request, sizeof(request)-1); // -1 to add '\0'
        if (rn > 0) {
            request[rn] = '\0';

            if (cmd_timer) {
                printf("EXECUTION: \033[33mSKIPPED\033[0m (%.2f seconds left)\n", (cmd_timer - now_ms()) / 1000.0 /* convert to seconds (float) */ );
                if (do_reply) {
                    move(CMD_STOP, 0);
                    reply(REPLY_SKIPPED);
                    do_reply = false;
                }
            }

            printf("RECEIVED COMMAND: '\033[90m%s\033[0m'\n", request);

            parse_cmd(&request_args, request);

            /* print info ***(verbose)*** */
            /* 
            printf("type:           ");
            switch (request_args.type) {
                case CMD_MOVE_FORWARD:
                    printf("CMD_MOVE_FORWARD\n");
                    break;
                case CMD_MOVE_BACK:
                    printf("CMD_MOVE_BACK\n");
                    break;
                case CMD_TURN_RIGHT:
                    printf("CMD_TURN_RIGHT\n");
                    break;
                case CMD_TURN_LEFT:
                    printf("CMD_TURN_LEFT\n");
                    break;
                case CMD_STOP:
                    printf("CMD_STOP\n");
                    break;
                case CMD_UNKNOWN:
                    printf("CMD_UNKNOWN\n");
                    break;
            }
            printf("seconds:        %lu\n", request_args.seconds);
            printf("reply:         %d\n", request_args.reply);
            */

            /* checking */
            if (request_args.type == CMD_UNKNOWN) {
                fprintf(stderr, "\033[31mERORR:\033[0m INVALID COMMAND\n");
                /* reset values */
                parse_cmd(&request_args, NULL);
            }
            else if ((request_args.type == CMD_MOVE_FORWARD || request_args.type == CMD_MOVE_BACK) && request_args.seconds <= 0) {
                fprintf(stderr, "\033[31mERORR:\033[0m MISSING SECONDS\n");
                parse_cmd(&request_args, NULL);
            }
            else if ((request_args.type == CMD_TURN_RIGHT || request_args.type == CMD_TURN_LEFT) && request_args.seconds) {
                fprintf(stderr, "\033[31mERORR:\033[0m INVALID OPTIONS\n");
                parse_cmd(&request_args, NULL);
            }
            else if (request_args.type == CMD_MOVE_FORWARD && blocked) { // block command before executing
                fprintf(stderr, "EXECUTION: \033[33mBLOCKED\033[0m\n");
                if (request_args.reply) {
                    reply(REPLY_BLOCKED);
                }
                parse_cmd(&request_args, NULL);
            }
            else {
                move_ret = move(request_args.type, request_args.seconds);

                if (move_ret < 0) {
                    fprintf(stderr, "EXECUTION: \033[31mFAILED\033[0m (%s)\n", pigpio_error(move_ret));
                    if (request_args.reply)
                        reply(REPLY_FAILED);
                    move_ret = -1;
                }
                else if (request_args.type == CMD_STOP) {
                    fprintf(move_ret < 0 ? stderr : stdout, "EXECUTION: %s\033[0m\n", move_ret < 0 ? "\033[31mFAILED" : "\033[32mSUCCESS");
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

    /*******************************************************/

    usleep(50000);

    /* stop motors */
    move(CMD_STOP, 0);

    pigpio_stop(); // disconnect from daemon (pigpiod)

    /* double check */
    if (CMD_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    /* remove FIFO files */
    if (unlink(CMD_FIFO) < 0)
        fprintf(stderr, "\033[33mWARNING:\033[0m unlink %s: %s", CMD_FIFO, strerror(errno));

    if (unlink(REPLY_FIFO) < 0)
        fprintf(stderr, "\033[33mWARNING:\033[0m unlink %s: %s", REPLY_FIFO, strerror(errno));

    if (unlink(PIDFILE) < 0)
        fprintf(stderr, "\033[33mWARNING:\033[0m unlink %s: %s", PIDFILE, strerror(errno));


    puts("\nexiting...");

    return 0;
}
