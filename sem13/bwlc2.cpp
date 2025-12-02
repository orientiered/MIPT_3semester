#include <cctype>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <cstring>
#include <poll.h>

#include "../utils.hpp"

const int BUF_SIZE = 4096;

pid_t Fork()
{
    pid_t ret = fork();
    if (ret == -1) {
        perror("Fork error:");
    }

    return ret;
}

const int NOT_LAUNCHED_CODE = 127;

void Execvp(const char *__file, char *const __argv[])
{
    int code = execvp(__file, __argv);
    if (code == -1) {
        perror("Execvp error");
        printf("Failed to execute %s\n", __file);
        exit(NOT_LAUNCHED_CODE);
    }

}

struct pipe_fd{
    int read_fd = -1;
    int write_fd = -1;
};

pipe_fd SafePipe() {
    int fd[2] = {-1, -1};
    if (pipe(fd) < 0) {
        perror("Failed to create pipe");
        exit(1);
    }

    return pipe_fd{fd[0], fd[1]};
}

struct ByteCounter {
private:
    bool is_word = false;
public:
    size_t bytes = 0, words = 0, lines = 0;

    void update(const char *buf, size_t len) {
        bytes += len;
        for (size_t idx = 0; idx < len; idx++) {
            char current = buf[idx];
            if (current == '\n') lines++;
            if (isspace(current)) {
                is_word = false;
            } else {
                if (!is_word) words++;
                is_word = true;
            }
        }
    }

    void dump() {
        printf("\nbytes %zu\nwords %zu\nlines %zu\n",
                  bytes,     words,     lines);
    }
};

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Expected program to run\n");
        return 0;
    }

    struct dup_fd{
        pipe_fd pipe;
        int fd;
    };

    dup_fd fds_arr[] = {{SafePipe(), STDOUT_FD}, {SafePipe(), STDERR_FD}};

    size_t fds_count = 2;

    LOG("Forking...\n");
    pid_t pid = Fork();

    if (!pid) {
        for (int i = 0; i < fds_count; i ++) {
            Close(fds_arr[i].pipe.read_fd);
            dup2(fds_arr[i].pipe.write_fd, fds_arr[i].fd);
            Close(fds_arr[i].pipe.write_fd);
        }

        Execvp(argv[1], &argv[1]);
    } else {

        for (int i = 0; i < fds_count; i ++) {
            Close(fds_arr[i].pipe.write_fd);
        }

        ByteCounter counter[fds_count];
        char buffer[BUF_SIZE];

        struct pollfd poll_fds[] = {
            {fds_arr[0].pipe.read_fd, POLLIN, 0},
            {fds_arr[1].pipe.read_fd, POLLIN, 0}
        };

        int active_fds = 2;
        while (active_fds > 0) {
            CHECK(poll(poll_fds, 2, -1), "poll err");
            // LOG("Polled %d events\n", events);

            for (int i = 0; i < fds_count; i++) {
                if (poll_fds[i].revents == 0) continue;

                if (poll_fds[i].revents & POLLIN) {
                    ssize_t bytes_read = Read(fds_arr[i].pipe.read_fd, buffer, BUF_SIZE);

                    if (bytes_read > 0) {
                        counter[i].update(buffer, bytes_read);
                    }

                } else {
                    Close(fds_arr[i].pipe.read_fd);
                    poll_fds[i].fd = -1; // reached end of file, removing fd from poll set
                    active_fds--;
                }

            }
        }

        int wstatus = 0;
        wait(&wstatus);
        if (WEXITSTATUS(wstatus) == NOT_LAUNCHED_CODE) {
            fprintf(stderr, "Failed to launch %s\n", argv[1]);
            return 1;
        }

        printf("Stdin:\n");
        counter[0].dump();

        printf("Stderr:\n");
        counter[1].dump();
    }

    return 0;
}
