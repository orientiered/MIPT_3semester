#include <cctype>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#include <cstring>

const int STDOUT_FD = 1;
const int STDIN_FD = 0;

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

ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t code = read(fd, buf, count);
    if (code < 0) {
        perror("read error");
        // exit(SYSCALL_ERR);
    }

    return code;
}

ssize_t Write(int fd, const void *buf, size_t count) {
    const uint8_t *byte_buf = (const uint8_t *)buf;

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

void Close(int fd) {
    int code = close(fd);
    if (code < 0) {
        perror("Close error");
        // exit(SYSCALL_ERR);
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

    pipe_fd fds = SafePipe();

    pid_t pid = Fork();

    if (!pid) {
        Close(fds.read_fd);
        dup2(fds.write_fd, STDOUT_FD);
        Close(fds.write_fd); // closing dangling pipe descriptor

        Execvp(argv[1], &argv[1]);
    } else {
        Close(fds.write_fd);

        ByteCounter counter;
        char buffer[BUF_SIZE];
        ssize_t bytes_read = 0;
        while((bytes_read = Read(fds.read_fd, buffer, BUF_SIZE)) > 0) {
            counter.update(buffer, bytes_read);
            Write(STDOUT_FD, buffer, bytes_read);
        }

        int wstatus = 0;
        wait(&wstatus);
        if (WEXITSTATUS(wstatus) == NOT_LAUNCHED_CODE) {
            return 1;
        }

        Close(fds.read_fd);
        counter.dump();
    }

    return 0;
}
