#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdlib.h>
#include <stdint.h>

#include <pthread.h>

#include "../utils.hpp"

#include <utility>

const size_t PAGE_SIZE = 4096;
const size_t PAGE_COUNT = 16;

const int STDOUT_FD = 1;
const int STDIN_FD = 0;



#define MUTEX(mtx, ...) \
    pthread_mutex_lock(mtx); \
    __VA_ARGS__ \
    pthread_mutex_unlock(mtx);

struct monitor {
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    char *buffer = nullptr;
    int lengths[PAGE_COUNT];
    int write_ptr = 0;
    int read_ptr = 0;
    int stop_flag = false;
    pthread_cond_t empty = PTHREAD_COND_INITIALIZER,
                   ready = PTHREAD_COND_INITIALIZER;

    monitor() {
        buffer = (char *) calloc(PAGE_COUNT, PAGE_SIZE);
        if (buffer == nullptr) {
            LOG("Calloc failed\n");
            exit(EXIT_FAILURE);
        }

        CHECK(pthread_mutex_init(&mtx, NULL), "mutex_init");
        CHECK(pthread_cond_init(&empty, NULL), "cond_init");
        CHECK(pthread_cond_init(&ready, NULL), "cond_init");
    }

    void destroy() {
        free(buffer);
        CHECK(pthread_mutex_destroy(&mtx), "mutex_destroy");
        CHECK(pthread_cond_destroy(&empty), "cond_destroy");
        CHECK(pthread_cond_destroy(&ready), "cond_destroy");
    }

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

    monitor mon;

    pthread_t tid = 0;
    CHECK(pthread_create(&tid, NULL, writer, &mon), "thread_create");

    // reading

    for (int arg_idx = 1; arg_idx < argc; arg_idx++) {
        read_file(&mon, argv[arg_idx]);
    }

    mon.writer_stop();

    // destroy

    CHECK(pthread_join(tid, NULL), "err");
    mon.destroy();

    return 0;
}
