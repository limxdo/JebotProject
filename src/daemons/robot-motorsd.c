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

/* FIFO files path */
#define CMD_FIFO        "/tmp/robot-cmd"
#define REPLY_FIFO      "/tmp/robot-reply"

/* GPIOs */
#define  RIGHT_MOTORS   17
#define  LEFT_MOTORS    27

bool RUNNING = true;
bool moving = false;
uint64_t cmd_timeout = 0;
bool do_reply = false;
int move_ret_reply = -1;

/* handler of signals */
void handler(int signum) {
    RUNNING = false;
    puts("");
}

uint64_t now_ms() {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    /*      convert sec to ms     convert ns to ms */
    return (ts.tv_sec * 1000) + (ts.tv_nsec / 1000000);
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
void parse_cmd(struct cmd_args *request_args, const char *request) {

    /* default values */
    request_args->type = CMD_UNKNOWN;
    request_args->seconds = 0;
    request_args->reply = false;

    char scopy[strlen(request)+1]; // +1 for '\0'
    strcpy(scopy, request);

    char *argptr;
    char *endul;    // for strtol

    argptr = strtok(scopy, " ");

    /* get type of request_args->type */
    if (argptr == NULL) {
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

    /* get seconds */
    argptr = strtok(NULL, " ");
    
    if (argptr == NULL) {
        return;
    }
    else {
        if (strcmp(argptr, "--reply") == 0) {
            request_args->reply = true;
            return;
        }
        else {
            request_args->seconds = strtol(argptr, &endul, 10);
            if (endul == argptr) {  // not a number
                request_args->seconds = 0;
            }
        }
    }
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
int move(enum cmd_type mode, unsigned long seconds) {

    /* 
     * NOTE: All GPIO operations in this function are currently temporary and for testing purposes only.
     * The actual motors / motor drivers are not available yet.
     * Currently, each GPIO controls two wheels (so 4 wheels total are split over 2 GPIOs).
     * This setup can be easily updated once the real motors / motor drivers are integrated.
     */

    /* convert sec to ms */
    seconds *= 1000;
    int gpio_write_status;

    unsigned long angle_90 = 2 * 1000; // 2 seconds (tmp) for example

    /* execute command and error checking */
    switch (mode) {
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
            cmd_timeout = now_ms() + seconds;
            moving = true;
            break;

        case CMD_MOVE_BACK:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 1)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 1)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timeout = now_ms() + seconds;
            moving = true;
            break;

        case CMD_TURN_RIGHT:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 1)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 0)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timeout = now_ms() + angle_90;
            moving = true;
            break;

        case CMD_TURN_LEFT:
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 0)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 1)) < 0)
            ) {
                move(CMD_STOP, 0);
                return gpio_write_status;
            }

            cmd_timeout = now_ms() + angle_90;
            moving = true;
            break;

        case CMD_STOP:
            /* do not call move(CMD_STOP, 0) again to avoid recursion if pigpiod failed */
            if (
                ((gpio_write_status = gpio_write(LEFT_MOTORS, 0)) < 0) ||
                ((gpio_write_status = gpio_write(RIGHT_MOTORS, 0)) < 0)
            )
                return gpio_write_status;

            cmd_timeout = 0;
            moving = false;
            break;
    }

    return 1; // return true
}

/* reply function */
void reply(int move_ret) {
    /*
     * move_ret can get it from move() function
     */

    usleep(20000); // delay before replying

    int REPLY_FIFO_FD = -1;
    char status[16];
    strcpy(status, move_ret < 0 ? "FAILED" : "SUCCESS");

    /* open REPLY_FIFO */
    if ((REPLY_FIFO_FD = open(REPLY_FIFO, O_WRONLY | O_NONBLOCK)) < 0) {
        fprintf(stderr, "\033[31mREPLY: ERROR\033[0m (%s)\n", strerror(errno));
        return;
    }
    else {
        if (write(REPLY_FIFO_FD, status, strlen(status)) < 0) {
            fprintf(stderr, "\033[31mREPLY: WRITE ERROR\033[0m (%s)\n", strerror(errno));
        } else {
            printf("\033[32mREPLY:\033[0m (SUCCESS)\n");
        }

        close(REPLY_FIFO_FD);
    }
}

int main(void) {

    printf("PID: %d\n\n", getpid());

    int conn = pigpio_start(NULL, NULL); // (NULL, NULL) uses to connect to local gpio daemon (pigpiod)

    /* signal handling */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGPIPE, SIG_IGN);

    if (conn < 0) {
        fprintf(stderr, "\033[31mFAILED connect to pigpiod:\033[0m %s\n", pigpio_error(conn));
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

    /* creating FIFO files */
    while (mkfifo(CMD_FIFO, 0666) < 0) {
        unlink(CMD_FIFO);
        fprintf(stderr, "\033[31mWARNING: mkfifo %s\033[0m: %s\n", CMD_FIFO, strerror(errno));
        usleep(50000) ;
    }
    //printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", CMD_FIFO); // (verbose)
    chmod(CMD_FIFO, 0666);

    while ((CMD_FIFO_FD = open(CMD_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
        perror("\033[31mWARNING: open CMD_FIFO_FD\033[0m");
        usleep(10000);
    }


    while (mkfifo(REPLY_FIFO, 0666) < 0) {
        unlink(REPLY_FIFO);
        fprintf(stderr, "\033[31mWARNING: mkfifo %s: \033[0m: %s\n", REPLY_FIFO, strerror(errno));
        usleep(50000) ;
    }
    chmod(REPLY_FIFO, 0666);
    //printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", REPLY_FIFO); // (verbose)

    /*******************************************************/

    char request[256];
    ssize_t rn;
    int move_ret = 0;

    struct cmd_args request_args;

    puts("\n|********** Daemon Is Started **********|\n");
    while(RUNNING) {

        if (moving && now_ms() >= cmd_timeout) {
            move(CMD_STOP, 0);
            if (do_reply) {
                do_reply = false;
                reply(move_ret_reply);
                move_ret_reply = -1;
            }
        }

        rn = read(CMD_FIFO_FD, request, sizeof(request)-1);
        if (rn > 0) {
            request[rn] = '\0';

            printf("\033[32mRECEIVED COMMAND:\033[0m '%s'\n", request);

            parse_cmd(&request_args, request);

            /* printf info ***(verbose)*** */
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
                case CMD_UNKNOWN:
                    printf("CMD_UNKNOWN\n");
                    break;
            }
            printf("seconds:        %lu\n", request_args.seconds);
            printf("reply:         %d\n", request_args.reply);
            */

            /* checking */
            if (request_args.type == CMD_UNKNOWN) {
                fprintf(stderr, "\033[31mERORR: INVALID COMMAND\033[0m\n");
                continue;
            }
            else if ((request_args.type == CMD_MOVE_FORWARD || request_args.type == CMD_MOVE_BACK) && request_args.seconds <= 0) {
                fprintf(stderr, "\033[31mERORR: MISSING SECONDS\033[0m\n");
                continue;
            }
            else if ((request_args.type == CMD_TURN_RIGHT || request_args.type == CMD_TURN_LEFT) && request_args.seconds) {
                fprintf(stderr, "\033[31mERORR: INVALID OPTIONS\033[0m\n");
                continue;
            }
            else {
                move_ret = move(request_args.type, request_args.seconds);

                if (move_ret < 0)
                    fprintf(stderr, "\033[32mEXECUTION:\033[0m FAILED (%s)\n", pigpio_error(move_ret));
                else
                    printf("\033[32mEXECUTION:\033[0m SUCCESS\n");

                if (request_args.reply && move_ret < 0) {
                    reply(move_ret);
                }
                else if (request_args.reply) {
                    do_reply = true;
                    move_ret_reply = move_ret;
                }
                else {
                    do_reply = false;
                    move_ret_reply = -1;
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

    /* check if FDs is opened and close it */
    if (CMD_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    /* remove FIFO files */
    if (unlink(CMD_FIFO) < 0)
        perror("\033[31mWARNING: unlink CMD_FIFO\033[0m");

    if (unlink(REPLY_FIFO) < 0)
        perror("\033[31mWARNING: unlink REPLY_FIFO\033[0m");


    puts("\nexiting...");

    return 0;
}

