#include "../include/runtime.h"
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>

#define SIZE 256

static struct {
    char base[SIZE];
    char pid[SIZE];

    char created_dirs[SIZE][256];
    char created_files[SIZE][256];

    int fds[SIZE];

    size_t dir_count, file_count, fd_count;

} runtime;

int runtime_init(const char *base, mode_t mode) {
    runtime.dir_count = 0;
    runtime.file_count = 0;
    runtime.fd_count = 0;

    snprintf(runtime.base, sizeof(runtime.base), RUNTIME_PATH "/%s", base);
    snprintf(runtime.pid, sizeof(runtime.pid), "%s/pid", runtime.base);

    if (mkdir(runtime.base, mode) != 0 && errno != EEXIST)
        return -1;
    chmod(runtime.base, mode);

    return 0;
}

int runtime_mkdir(const char *name, mode_t mode) {

    char dir_path[SIZE];
    snprintf(dir_path, sizeof(dir_path), "%s/%s", runtime.base, name);

    if (mkdir(dir_path, mode) != 0 && errno != EEXIST)
        return -1;
    chmod(dir_path, mode);

    strncpy(runtime.created_dirs[runtime.dir_count++], dir_path, 256);
    return 0;
}

int runtime_open(const char *path, int flags, mode_t mode) {

    char file_path[SIZE];
    snprintf(file_path, sizeof(file_path), "%s/%s", runtime.base, path);

    int fd = open(file_path, flags, mode);
    if (fd < 0) return -1;

    strncpy(runtime.created_files[runtime.file_count++], file_path, 256);
    runtime.fds[runtime.fd_count++] = fd;

    return fd;
}

int runtime_pid(pid_t pid) {
    FILE *f = fopen(runtime.pid, "w");
    if (!f) return -1;

    fprintf(f, "%d\n", pid);
    fclose(f);

    return 0;
}

void runtime_exit(void) {
    for (size_t i = 0; i < runtime.file_count; i++) {
        close(runtime.fds[i]);
        unlink(runtime.created_files[i]);
    }
    unlink(runtime.pid);

    for (size_t i = runtime.dir_count; i > 0; i--)
        rmdir(runtime.created_dirs[i-1]);

    rmdir(runtime.base);
}
