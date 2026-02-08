#include <pigpio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define CMD_FIFO        "robot-cmd"
#define REPLAY_FIFO     "robot-replay"

int status = 0;
int RUNNING = 1;

/* handler of signals */
void handler(int signum) {
    RUNNING = 0;
    puts("");
}

/* main function to moving */
int move(const char *mode, size_t seconds) {
    return 1;
}

int main(void) {

    printf("PID: %d\n", getpid());

    //int gpioinit = gpioInitialise();
    int gpioinit = 1;

    /* signal handling */
    signal(SIGINT, handler);
    signal(SIGTERM, handler);
    signal(SIGPIPE, SIG_IGN);

    if (gpioinit < 0) {
        fprintf(stderr, "\033[31mFAILED to Initialise GPIO\033[0m\n");
        return 1;
    }


    /* file descreptors */
    int CMD_FIFO_FD = -1, REPLAY_FIFO_FD = -1;

    /* creating FIFOs files */
    while (mkfifo(CMD_FIFO, 0666) < 0) {
        unlink(CMD_FIFO);
        perror("\033[31mWARNING: mmkfifo CMD_FIFO\033[0m");
        usleep(50000) ;
    }
    printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", CMD_FIFO);
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
    printf("\033[32mCreating\033[0m '%s' \033[32mSuccess\033[0m\n", REPLAY_FIFO);

    /* while ((REPLAY_FIFO_FD = open(REPLAY_FIFO, O_WRONLY | O_NONBLOCK)) < 0) {
        perror("\033[31mWARNING: open CMD_FIFO_FD\033[0m");
        usleep(10000);
    } */

    /*******************************************************/

    char request[256];
    ssize_t rn, wn;

    puts("\n|********** Daemon Is Started **********|\n");
    while(RUNNING) {
        rn = read(CMD_FIFO_FD, request, sizeof(request)-1);
        if (rn > 0) {
            request[rn] = '\0';

            printf("\033[32mGETTED COMMAND:\033[0m %s\n", request);
        }

        usleep(1);
    }
    usleep(50000);

    /*******************************************************/

    //gpioTerminate();

    /* check if FDs is opned and close it */
    if (CMD_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    if (REPLAY_FIFO_FD > 0)
        close(CMD_FIFO_FD);

    /* remove FIFO files */
    if (unlink(CMD_FIFO) < 0)
        perror("\033[31mWARNING: unlink CMD_FIFO\033[0m");

    if (unlink(REPLAY_FIFO) < 0)
        perror("\033[31mWARNING: unlink REPLAY_FIFO\033[0m");


    printf("\nexiting...\n");

    return status;
}
