#pragma once

#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>

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
