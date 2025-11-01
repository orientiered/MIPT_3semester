#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <semaphore.h>
#include <string.h>

const char * const PIZZA_str  = "pizza!";
const char * const CLOSED = "closed";
const size_t pizza_len = strlen(PIZZA_str) + 1;

struct context {
    sem_t *ready;
    sem_t *empty;

    sem_t *mutex;

    char *sh_tables;
    int  *sh_put;
    int  *sh_get;
    int  *continue_work;
};

/*
sem ready = 0
sem empty = N

sem mutex
shared put = 0
shared get = 0
shared continue_work = 1
chief:
    wait(empty)

    wait(mutex)
    my_put = put;
    put++; put %= N;
    post(mutex)

    cook(my_put)

    post(ready)
client:
    wait(ready)

    wait(mutex)
    my_get = get;
    get++; get %= N;
    post(mutex)

    check(my_get)

    post(empty)

*/

int circular_inc(int *val, int N) {
    int current = *val;
    *val = (current + 1) % N;
    return current;
}

#define MUTEX(...) do {sem_wait(ctx.mutex); __VA_ARGS__ sem_post(ctx.mutex); } while(0);
void producer [[noreturn]] (volatile context ctx, int N) {
    while (true) {
        int put_idx = 0;
        sem_wait(ctx.empty);
        MUTEX(
            put_idx = circular_inc(ctx.sh_put, N);
        )

        strcpy(ctx.sh_tables + put_idx*pizza_len, PIZZA_str);
        fprintf(stderr, "%d:Cooked pizza at table %d\n", getpid()%100, put_idx);

        sem_post(ctx.ready);
        bool continue_work = true;
        MUTEX(continue_work = *ctx.continue_work;)
        if (!continue_work) break;
        // usleep(5000);
    }

    fprintf(stderr, "%d:Finished work\n", getpid()%100);

    exit(0);
}

void client [[noreturn]] (volatile context ctx, int N) {
    while (true) {
        int get_idx = 0;
        sem_wait(ctx.ready);
        MUTEX(
            get_idx = circular_inc(ctx.sh_get, N);
        )

        if (strcmp(ctx.sh_tables + get_idx*pizza_len, PIZZA_str) == 0) {
            fprintf(stderr, "\t\t\t\t\t\t%d:Ate pizza at table %d\n", getpid()%100, get_idx);
        } else {
            fprintf(stderr, "\t\t\t\t\t\t%d:This is not a pizza %d\n", getpid()%100, get_idx);
        }
        sem_post(ctx.empty);

            bool continue_work = true;
        MUTEX(continue_work = *ctx.continue_work;)
        if (!continue_work) break;
        // usleep(5000);
    }

    exit(0);
}

#define CHECK(expr, msg) if ((long long)(expr) < 0) {perror(msg); exit(EXIT_FAILURE); }

sem_t *Create_sem(const char *name, int value) {
    sem_t *sem = sem_open(name, O_CREAT, 0666, value);
    CHECK(sem, name);

    return sem;
}


const char * const SHM_NAME = "/pz_shm";
const char * const SEM_READY_NAME = "/pz_ready";
const char * const SEM_EMPTY_NAME = "/pz_empty";
const char * const SEM_MUTEX_NAME = "/pz_mutex";

int main(int argc, const char *argv[]) {
    int N = 5;

    int chiefs = 7;
    int consumers = 8;

    if (argc == 4) {
        sscanf(argv[1], "%d", &N);
        sscanf(argv[2], "%d", &chiefs);
        sscanf(argv[3], "%d", &consumers);
    }

    int shm_fd = shm_open(SHM_NAME, O_RDWR | O_CREAT, 0666);
    CHECK(shm_fd, "shm error");
    size_t full_size = pizza_len * N + 3*sizeof(int);
    CHECK(ftruncate(shm_fd, pizza_len), "truncate err");
    char *sh_mem = (char*) mmap(NULL, full_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    CHECK(sh_mem, "mmap");

    context ctx = {
        .ready = Create_sem(SEM_READY_NAME, 0),
        .empty = Create_sem(SEM_EMPTY_NAME, N),
        .mutex = Create_sem(SEM_MUTEX_NAME, 1),

        .sh_tables = sh_mem + 2*sizeof(int),
        .sh_put = (int *) sh_mem,
        .sh_get = ((int *) sh_mem ) + 1,
        .continue_work = ((int *) sh_mem ) + 2
    };

    *ctx.continue_work = 1;

    for (int i = 0; i < chiefs; i++) {
        pid_t pid = fork();
        CHECK(pid, "Fork error");
        if (pid == 0) {
            producer(ctx, N);
        }
    }

    for (int i = 0; i < chiefs; i++) {
        pid_t pid = fork();
        CHECK(pid, "Fork error");
        if (pid == 0) {
            client(ctx, N);
        }
    }

    usleep(2000);
    MUTEX(
        *ctx.continue_work = 0;
    )

    fprintf(stderr, "CLOSING\nCLOSING\nCLOSING\n");
    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}

    CHECK(munmap(sh_mem, full_size), "unmap");
    CHECK(shm_unlink(SHM_NAME), "unlink");
    CHECK(sem_unlink(SEM_READY_NAME), "sem-unlink");
    CHECK(sem_unlink(SEM_EMPTY_NAME), "sem-unlink");
    CHECK(sem_unlink(SEM_MUTEX_NAME), "sem-unlink");

    return 0;
}
