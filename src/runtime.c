#include "../include/runtime.h"
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <dirent.h>

#define SIZE 256

static struct {
    char base[SIZE];
    char pid[SIZE];

    int fds[SIZE];

    size_t fd_count;

} runtime;

int runtime_init(const char *base, mode_t mode) {
    runtime.fd_count = 0;

    snprintf(runtime.base, sizeof(runtime.base), RUNTIME_PATH "/%s", base);
    snprintf(runtime.pid, sizeof(runtime.pid), "%s/pid", runtime.base);

    /* create RUNTUME_PATH if not exists */
    if (mkdir(RUNTIME_PATH, 0755) != 0 && errno != EEXIST)
        return -1;
    chmod(RUNTIME_PATH, 0755);

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

    return 0;
}

int runtime_open(const char *path, int flags, ...) {
    char file_path[SIZE];
    snprintf(file_path, sizeof(file_path), "%s/%s", runtime.base, path);
    mode_t mode = 0;
    int fd;

    if (flags & O_CREAT) {
        va_list args;
        va_start(args, flags);
        mode = va_arg(args, mode_t);
        va_end(args);
        fd = open(file_path, flags, mode);
    }
    else {
        fd = open(file_path, flags);
    }

    if (fd < 0) return -1;
    if (mode) chmod(file_path, mode);

    runtime.fds[runtime.fd_count++] = fd;

    return fd;
}

int runtime_fifo(const char *path, mode_t mode) {
    char full_path[SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", runtime.base, path);

    unlink(full_path);;
    if (mkfifo(full_path, mode) < 0) return -1;
    chmod(full_path, mode);

    return 0;
}

int runtime_write_atomic(const char *path, mode_t mode, const char *format, ...) {
    char file_path[SIZE];
    snprintf(file_path, sizeof(file_path), "%s/%s", runtime.base, path);

    char tmp_file_path[SIZE];
    snprintf(tmp_file_path, sizeof(tmp_file_path), "%s/%s.tmp", runtime.base, path);

    int fd = open(tmp_file_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (fd < 0) return -1;

    va_list args;
    va_start(args, format);
    vdprintf(fd, format, args);
    va_end(args);

    close(fd);

    if (rename(tmp_file_path, file_path) < 0) {
        unlink(tmp_file_path);
        return -1;
    }

    return 0;
}

int runtime_pid(pid_t pid) {
    int fd = open(runtime.pid, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    dprintf(fd, "%d\n", pid);
    close(fd);

    return 0;
}

static int rmdir_recursive(const char *dir_path) {
    struct dirent *entry;
    struct stat st;

    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    char full_path[SIZE];

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name); // if dir
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            rmdir_recursive(full_path);
        } else {
            unlink(full_path);
        }
    }

    closedir(dir);

    rmdir(dir_path);

    return 0;
}

void runtime_exit(void) {
    for (size_t i = 0; i < runtime.fd_count; i++) {
        close(runtime.fds[i]);
    }

    rmdir_recursive(runtime.base);

    /* if empty */
    rmdir(RUNTIME_PATH);
}
