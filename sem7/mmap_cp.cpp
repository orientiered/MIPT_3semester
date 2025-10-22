#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>

#define CHECK(expr, msg) if ((long long)(expr) < 0) {perror(msg); exit(EXIT_FAILURE); }

int main(int argc, const char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: cp src dst\n");
        exit(EXIT_FAILURE);
    }

    int fd_src = open(argv[1], O_RDONLY);
    CHECK(fd_src, argv[1]);

    struct stat statbuf = {};
    CHECK(fstat(fd_src, &statbuf), "stat error");

    void *src = mmap(NULL, statbuf.st_size, PROT_READ, MAP_PRIVATE, fd_src, 0);
    CHECK(src, "mmap src");

    CHECK(close(fd_src), "close");

    CHECK(umask(0), "umask");
    int fd_dst = open(argv[2], O_RDWR | O_CREAT, 0666);
    CHECK(fd_dst, argv[2]);

    CHECK(ftruncate(fd_dst, statbuf.st_size), "ftruncate");
    void *dst = mmap(NULL, statbuf.st_size, PROT_WRITE, MAP_SHARED, fd_dst, 0);
    CHECK(dst, "mmap dst");

    memcpy(dst, src, statbuf.st_size);
    // CHECK(fsync(fd_dst), "sync");
    CHECK(munmap(dst, statbuf.st_size), "unmap dst");
    CHECK(munmap(src, statbuf.st_size), "unmap src");

    CHECK(close(fd_dst), "close");
    return 0;
}
