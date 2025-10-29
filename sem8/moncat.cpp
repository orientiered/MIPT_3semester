#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>

#include <pthread.h>

// ONLY FOR PERROR
#include <stdio.h>
#include <utility>

const size_t PAGE_SIZE = 4096;
const size_t PAGE_COUNT = 16;

const int SYSCALL_ERR = 4;

const int STDOUT_FD = 1;
const int STDIN_FD = 0;

#define LOG(...) fprintf(stderr, __VA_ARGS__)
#define CHECK(code, msg) if ((code) < 0) {perror(msg); exit(EXIT_FAILURE); }

ssize_t Read(int fd, void *buf, size_t count) {
    ssize_t code = read(fd, buf, count);
    if (code < 0) {
        perror("read error");
        // exit(SYSCALL_ERR);
    }

    return code;
}

ssize_t Write(int fd, const void *buf, size_t count) {
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


int Open(const char *path, int flags) {
    int fd = open(path, flags);
    if (fd < 0) {
        perror(path);
    }

    return fd;
}

void Close(int fd) {
    int code = close(fd);
    if (code < 0) {
        perror("Close error");
        // exit(SYSCALL_ERR);
    }
}

#define MUTEX(mtx, ...) \
    pthread_mutex_lock(mtx); \
    __VA_ARGS__ \
    pthread_mutex_unlock(mtx);

struct monitor {
    pthread_mutex_t mtx;
    char *buffer;
    int lengths[PAGE_COUNT];
    int write_ptr;
    int read_ptr;
    int stop_flag;
    pthread_cond_t empty, ready;

    char* get_write_page() {
        int real_idx = write_ptr % PAGE_COUNT;

        MUTEX(&mtx,
        while (write_ptr > read_ptr && (real_idx == read_ptr % PAGE_COUNT)) {
            pthread_cond_wait(&empty, &mtx);
        }
        )

        return buffer + PAGE_SIZE * real_idx;
    }

    void return_writed_page(int len) {
        MUTEX(&mtx,
        lengths[write_ptr % PAGE_COUNT] = len;

        bool do_signal = read_ptr == write_ptr;
        write_ptr++;
        if (do_signal) {
            pthread_cond_signal(&ready);
        }
        )

        return;
    }

    void writer_stop() {
        MUTEX(&mtx,
        stop_flag = true;
        LOG("Reader: stopped\n");
        pthread_cond_signal(&ready);
        )
    }

    std::pair<const char *, int> get_read_page() {
        int real_idx = read_ptr % PAGE_COUNT;

        MUTEX(&mtx,
        while (read_ptr == write_ptr && !stop_flag) {
            pthread_cond_wait(&ready, &mtx);
        }
        )

        if (read_ptr >= write_ptr) return {nullptr, 0};
        return {buffer + real_idx * PAGE_SIZE, lengths[real_idx]};
    }

    void return_read_page() {
        MUTEX(&mtx,
        if (read_ptr % PAGE_COUNT == write_ptr % PAGE_COUNT) {
            pthread_cond_signal(&empty);
        }

        read_ptr++;
        )
    }
};

int read_file(monitor *mon, const char *file_name) {
    struct stat statbuf;
    if (stat(file_name, &statbuf) < 0) {
        perror(file_name);
        return 0;
    }

    int fd = open(file_name, O_RDONLY);
    char read_buf[PAGE_SIZE];

    int bytes_read = 0;
    while ((bytes_read = Read(fd, read_buf, sizeof(read_buf))) > 0) {
        char *page = mon->get_write_page();
        LOG("Reader: got page %p\n", page);
        memcpy(page, read_buf, bytes_read);
        LOG("Reader: returned page %p with %d bytes\n", page, bytes_read);
        mon->return_writed_page(bytes_read);
    }

    Close(fd);

    return 0;
}

int write_page(monitor *mon) {
    std::pair<const char *, int> page = mon->get_read_page();
    LOG("Writer: got page %p with %d bytes\n", page.first, page.second);

    if (page.first == nullptr) return 0;
    Write(STDOUT_FD, page.first, page.second);

    LOG("Writer: returned page %p\n", page.first);
    mon->return_read_page();

    return 1;

}

void *writer(void *mon_ptr) {
    monitor *mon = (monitor*) mon_ptr;

    while (true) {
        if (write_page(mon) <= 0) break;
        // usleep(1000);
    }

    return NULL;
}

int main(int argc, const char *argv[]) {

    monitor mon = {
        PTHREAD_MUTEX_INITIALIZER,
        (char*) calloc(PAGE_COUNT, PAGE_SIZE),
        {},
        0,
        0,
        false,
        PTHREAD_COND_INITIALIZER,
        PTHREAD_COND_INITIALIZER
    };
    CHECK(pthread_mutex_init(&mon.mtx, NULL), "err");
    CHECK(pthread_cond_init(&mon.empty, NULL), "err");
    CHECK(pthread_cond_init(&mon.ready, NULL), "err");
    pthread_t tid = 0;
    CHECK(pthread_create(&tid, NULL, writer, &mon), "err");

    // reading

    for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
        read_file(&mon, argv[arg_idx]);
    }

    mon.writer_stop();

    // destroy

    CHECK(pthread_join(tid, NULL), "err");
    CHECK(pthread_mutex_destroy(&mon.mtx), "err");
    CHECK(pthread_cond_destroy(&mon.empty), "err");
    CHECK(pthread_cond_destroy(&mon.ready), "err");
    free(mon.buffer);

    return 0;
}
