#pragma once

#include <cstdint>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <errno.h>

#define SPAWN(__VA_ARGS__) \
    do {                            \
        pid_t pid = fork();         \
        CHECK(pid, "Fork error");   \
        if (pid == 0) { __VA_ARGS__ } \
    } while(0)

#define CHECK(expr, msg) if ((long long)(expr) < 0) {perror(msg); exit(EXIT_FAILURE); }
#define SOFT_CHECK(expr, msg) if ((long long)(expr) < 0) {perror(msg); }
#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define PROC_LOG(fmt, ...) LOG("| %d | " fmt, getpid() % 100 ,##__VA_ARGS__)

// void test() {
//     PROC_LOG("hello %s %d", "world", 5);
//     PROC_LOG("hello");
// }

#define COL_RESET "\033[0m"
#define COL_GREEN "\033[32m"

inline void wait_for_all() {
    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}
}

inline ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t code = read(fd, buf, count);
    SOFT_CHECK(code, "read error");

    return code;
}

inline ssize_t Write(int fd, const void *buf, size_t count) {
    const uint8_t *byte_buf = (const uint8_t*) buf;

    while (count > 0) {
        ssize_t written = write(fd, byte_buf, count);
        if (written < 0) {
            if (errno == EINTR)
                continue;

            perror("write error");
            // exit(SYSCALL_ERR);
            return written;
        }

        count -= written;
        byte_buf += written;
    }

    return count;
}


inline int Open(const char *path, int flags) {
    int fd = open(path, flags);
    SOFT_CHECK(fd, path);

    return fd;
}

inline void Close(int fd) {
    int code = close(fd);
    SOFT_CHECK(code, "Close error");
}
