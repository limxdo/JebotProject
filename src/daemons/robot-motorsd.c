#include <pigpio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

/* FIFO files path */
#define CMD_FIFO        "robot-cmd"
#define REPLAY_FIFO     "robot-replay"

unsigned int RUNNING = 1;

/* handler of signals */
void handler(int signum) {
    RUNNING = 0;
    puts("");
}


/* enum command types */
enum cmd_type {
    CMD_MOVE_FORWARD,
    CMD_MOVE_BACK,
    CMD_TURN_RIGHT,
    CMD_TURN_LEFT,
    CMD_UNKNOWN
};

/* struct to store command & args from CMD_FIFO */
struct cmd_args {
    enum cmd_type type;
    unsigned long seconds;
    bool replay;
};

/* function to filter a struct from one string */
void parse_cmd(struct cmd_args *cmds, const char *s) {

    /* default values */
    cmds->type = CMD_UNKNOWN;
    cmds->seconds = 0;
    cmds->replay = false;

    char scopy[strlen(s)+1]; // +1 for '\0'
    strcpy(scopy, s);

    char *argptr;
    char *endul;    // for strtoul

    argptr = strtok(scopy, " ");

    /* get type of cmds->type */
    if (argptr == NULL) {
        return;
    }
    else {
        /* place values with ENUMs */
        if (strcmp(argptr, "MOVE_FORWARD") == 0) {
            cmds->type = CMD_MOVE_FORWARD;
        }
        else if (strcmp(argptr, "MOVE_BACK") == 0) {
            cmds->type = CMD_MOVE_BACK;
        }
        else if (strcmp(argptr, "TURN_RIGHT") == 0) {
            cmds->type = CMD_TURN_RIGHT;
        }
        else if (strcmp(argptr, "TURN_LEFT") == 0) {
            cmds->type = CMD_TURN_LEFT;
        }
        else {
            cmds->type = CMD_UNKNOWN;
            return;
        }
    }

    /* get seconds */
    argptr = strtok(NULL, " ");
    
    if (argptr == NULL) {
        return;
    }
    else {
        if (strcmp(argptr, "--replay") == 0) {
            cmds->replay = true;
            return;
        }
        else {
            cmds->seconds = strtoul(argptr, &endul, 10);
            if (endul == argptr) {  // not a number
                cmds->seconds = 0;
            }
        }
    }
    argptr = strtok(NULL, " ");

    if (argptr == NULL) {
        return;
    }
    else {
        if (strcmp(argptr, "--replay") == 0) {
            cmds->replay = true;
        }
    }
}

/* main function to moving */
int move(enum cmd_type mode, unsigned long seconds) {
    /* continue this function later */
    return 1; // return true
}

/* replay function */
void replay(int exec_status) {
    /*
     * exec_status can get it from move() function
     */

    int REPLAY_FIFO_FD = -1;
    char msg[16];
    strcpy(msg, exec_status ? "SUCCESS" : "FAILED");

    printf("\033[32mEXECUTION:\033[0m (%s)\n", msg);

    /* open REPLAY_FIFO */
    if ((REPLAY_FIFO_FD = open(REPLAY_FIFO, O_WRONLY | O_NONBLOCK)) < 0) {
        fprintf(stderr, "\033[31mREPLAY: ERROR\033[0m (%s)\n", strerror(errno));
        return;
    }
    else {
        if (write(REPLAY_FIFO_FD, msg, strlen(msg)) < 0) {
            fprintf(stderr, "\033[31mREPLAY: WRITE ERROR\033[0m (%s)\n", strerror(errno));
        } else {
            printf("\033[32mREPLAY:\033[0m (%s)\n", msg);
        }

        close(REPLAY_FIFO_FD);
    }
}

int main(void) {

    printf("PID: %d\n", getpid());

    /* signal handling */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGPIPE, SIG_IGN);


    /* robot-cmd FD */
    int CMD_FIFO_FD = -1;

    /* creating FIFOs files */
    while (mkfifo(CMD_FIFO, 0666) < 0) {
        unlink(CMD_FIFO);
        perror("\033[31mWARNING: mmkfifo CMD_FIFO\033[0m");
        usleep(50000) ;
    }
    //printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", CMD_FIFO); // (verbose)
    chmod(CMD_FIFO, 0666);

    while ((CMD_FIFO_FD = open(CMD_FIFO, O_RDONLY | O_NONBLOCK)) < 0) {
        perror("\033[31mWARNING: open CMD_FIFO_FD\033[0m");
        usleep(10000);
    }


    while (mkfifo(REPLAY_FIFO, 0666) < 0) {
        unlink(REPLAY_FIFO);
        perror("\033[31mWARNING: mkfifo REPLAY_FIFO FAILED\033[0m");
        usleep(50000) ;
    }
    chmod(REPLAY_FIFO, 0666);
    //printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", REPLAY_FIFO); // (verbose)

    /*******************************************************/

    char request[256];
    ssize_t rn;
    int exec_status = 0;

    struct cmd_args commands;

    puts("\n|********** Daemon Is Started **********|\n");
    while(RUNNING) {
        rn = read(CMD_FIFO_FD, request, sizeof(request)-1);
        if (rn > 0) {
            request[rn] = '\0';

            printf("\033[32mRECEIVED COMMAND:\033[0m '%s'\n", request);

            parse_cmd(&commands, request);

            /* printf info ***(verbose)*** */
            /* 
            printf("type:           ");
            switch (commands.type) {
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
            printf("seconds:        %lu\n", commands.seconds);
            printf("replay:         %d\n", commands.replay);
            */

            /* checking */
            if (commands.type == CMD_UNKNOWN) {
                fprintf(stderr, "\033[31mERORR: INVALID COMMAND\033[0m\n");
                continue;
            }
            else if ((commands.type == CMD_MOVE_FORWARD || commands.type == CMD_MOVE_BACK) && !commands.seconds) {
                fprintf(stderr, "\033[31mERORR: MISSING SECONDS\033[0m\n");
                continue;
            }
            else if ((commands.type == CMD_TURN_RIGHT || commands.type == CMD_TURN_LEFT) && commands.seconds) {
                fprintf(stderr, "\033[31mERORR: INVALID OPTIONS\033[0m\n");
                continue;
            }
            else {
                if (commands.type == CMD_TURN_RIGHT)
                    exec_status = move(CMD_TURN_RIGHT, 0);

                else if (commands.type == CMD_TURN_LEFT)
                    exec_status = move(CMD_TURN_LEFT, 0);

                else if (commands.type == CMD_MOVE_FORWARD)
                    exec_status = move(CMD_MOVE_FORWARD, commands.seconds);

                else if (commands.type == CMD_MOVE_BACK)
                    exec_status = move(CMD_MOVE_BACK, commands.seconds);

                if (commands.replay) {
                    replay(exec_status);
                }
            }
        }

        usleep(1);
    }
    usleep(50000);

    /*******************************************************/

    //gpioTerminate();

    /* check if FDs is opned and close it */
    if (CMD_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    /* remove FIFO files */
    if (unlink(CMD_FIFO) < 0)
        perror("\033[31mWARNING: unlink CMD_FIFO\033[0m");

    if (unlink(REPLAY_FIFO) < 0)
        perror("\033[31mWARNING: unlink REPLAY_FIFO\033[0m");


    printf("\nexiting...\n");

    return 0;
}
