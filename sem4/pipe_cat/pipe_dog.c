#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

// ONLY FOR PERROR
#include <stdio.h>

const size_t BUF_SIZE = 4096;
const int SYSCALL_ERR = 4;

const int STDOUT_FD = 1;
const int STDIN_FD = 0;

ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t code = read(fd, buf, count);
    if (code < 0) {
        perror("read error");
        // exit(SYSCALL_ERR);
    }

    return code;
}

ssize_t Write(int fd, const void *buf, size_t count) {
    const uint8_t *byte_buf = buf;

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


int Open(const char *path, int flags) {
    int fd = open(path, flags);
    if (fd < 0) {
        perror(path);
    }

    return fd;
}

void copyFile(int fd_in, int fd_out) {
    char buffer[BUF_SIZE];
    int bytes_read = 0;
    while ((bytes_read = Read(fd_in, buffer, sizeof(buffer))) > 0) {
        Write(fd_out, buffer, bytes_read);
    }

}

void Close(int fd) {
    int code = close(fd);
    if (code < 0) {
        perror("Close error");
        // exit(SYSCALL_ERR);
    }
}

int main(int argc, const char *argv[]) {

    int pipefd[2] = {-1, -1};
    if (pipe(pipefd) < 0) {
        perror("Failed to create pipe");
        exit(1);
    }

    pid_t pid = fork();
    if (pid == 0) {
        // child is writer to stdout
        Close(pipefd[1]);
        copyFile(pipefd[0], STDOUT_FD);
        Close(pipefd[0]);
        return 0;
    } else {
        // parent reads from files and writes to pipe
        Close(pipefd[0]);
        if (argc == 1) {
            copyFile(STDIN_FD, pipefd[1]);
        } else {
            for (int idx = 1; idx < argc; idx++) {
                int fd = Open(argv[idx], O_RDONLY);
                if (fd < 0)
                    continue;
                copyFile(fd, pipefd[1]);

                Close(fd);
            }
        }
        Close(pipefd[1]);
    }


    return 0;
}
