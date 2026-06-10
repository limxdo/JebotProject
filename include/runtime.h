#ifndef RUNTIME_H
#define RUNTIME_H

#include <sys/stat.h>
#include <sys/types.h>

#define RUNTIME_PATH "/run/jebot"

int runtime_init(const char *base, mode_t mode);
int runtime_mkdir(const char *name, mode_t mode);
int runtime_open(const char *path, int flags, ...);
int runtime_fifo(const char *path, mode_t mode);
int runtime_write_atomic(const char *path, mode_t mode, const char *format, ...);
int runtime_pid(pid_t pid);
void runtime_exit(void);

#endif
