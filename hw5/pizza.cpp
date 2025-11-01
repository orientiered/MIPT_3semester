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

#include "../shared_mem.hpp"

const char * const PIZZA_str  = "pizza!";
// const char * const CLOSED = "closed";
const size_t pizza_len = strlen(PIZZA_str) + 1;

struct context {
    sem_t *ready;
    sem_t *empty;

    sem_t *mutex;

    int N;
    char *sh_tables;
    int  *sh_usage;
    int  *continue_work;
};

enum TABLE_TYPE {
    EMPTY = 0,
    CHIEF,
    CLIENT,
    PIZZA
};

const char * const str_table_type[] = {
    "EMPTY",
    "CHIEF",
    "CLIENT",
    "PIZZA"
};
void log_tables(context ctx) {
    for (int i = 0; i < ctx.N; i++) {
        LOG("%d:%s ", i, str_table_type[ctx.sh_usage[i]] );
    }

    LOG("\n");
}

// #define LOG_TABLES

#ifdef LOG_TABLES
# define SHOW_TABLES(ctx) log_tables(ctx)
#else
# define SHOW_TABLES(ctx)
#endif

/*
sem ready = 0
sem empty = N

sem mutex = 1
shared char tables[N][strlen(pizza)];
shared int  table_usage[N];
shared int  continue_work = 1

define mutex{...} wait(mutex) ... post(mutex)

chief:
    wait(empty)

  mutex{
    idx = find_empty(table_usage);
    table_usage[idx] = chief
  }

    cook(idx)

  mutex{
    table_usage[idx] = pizza
    post(ready)
  }

client:
    wait(ready)

  mutex{
    idx = find_ready(table_usage);
    table_usage[idx] = client
  }

    check(idx)

  mutex{
    table_usage[idx] = empty
    post(empty)
  }

*/


void work() {
    int work_time = rand() % 10 * 1e5;
    usleep(work_time);
}

int find_table(const int* tables, int N, int type) {
    for (int i = 0; i < N; i++) {
        if (tables[i] == type) return i;
    }

    LOG("!!!Table search failed\n");
    return -1;
}

#define MUTEX(...) do {sem_wait(ctx.mutex); __VA_ARGS__ sem_post(ctx.mutex); } while(0);
void producer [[noreturn]] (context ctx) {
    while (true) {
        int put_idx = 0;
        if (!*ctx.continue_work) break;
        if (sem_wait(ctx.empty) < 0) break;
        MUTEX(
            put_idx = find_table(ctx.sh_usage, ctx.N, EMPTY);
            ctx.sh_usage[put_idx] = CHIEF;
            SHOW_TABLES(ctx);
        )

        work();
        strcpy(ctx.sh_tables + put_idx*pizza_len, PIZZA_str);

        MUTEX(
            ctx.sh_usage[put_idx] = PIZZA;
            SHOW_TABLES(ctx);
            if (sem_post(ctx.ready) < 0) {
                sem_post(ctx.mutex);
                break;
            }
        )

        PROC_LOG("Cooked pizza at table %d\n", put_idx);
        // usleep(5000);
    }

    PROC_LOG("Chief: finished work\n");

    exit(0);
}

void client [[noreturn]] (context ctx) {
    while (true) {
        int get_idx = 0;
        if (!*ctx.continue_work) break;
        if (sem_wait(ctx.ready) < 0) break;
        MUTEX(
            get_idx = find_table(ctx.sh_usage, ctx.N, PIZZA);
            ctx.sh_usage[get_idx] = CLIENT;
            SHOW_TABLES(ctx);
        )

        work();
        bool check = strcmp(ctx.sh_tables + get_idx*pizza_len, PIZZA_str) == 0;

        MUTEX(
            ctx.sh_usage[get_idx] = EMPTY;
            SHOW_TABLES(ctx);

            if (sem_post(ctx.empty) < 0) {
                sem_post(ctx.mutex);
                break;
            }
        )

        if (check) {
            PROC_LOG("\t\t:Ate pizza at table %d\n", get_idx);
        } else {
            PROC_LOG("\t\t:This is not a pizza %d\n", get_idx);
        }
        // usleep(5000);
    }

    PROC_LOG("Client: finished work\n");

    exit(0);
}

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

    size_t full_size = pizza_len * N + N*sizeof(int) + sizeof(int);
    shmem_manager shmem(SHM_NAME, full_size);

    context ctx = {
        // .ready = NULL,
        .ready = Create_sem(SEM_READY_NAME, 0),
        .empty = Create_sem(SEM_EMPTY_NAME, N),
        .mutex = Create_sem(SEM_MUTEX_NAME, 1),
        .N = N,
        .sh_tables = shmem.get_shared<char>(pizza_len * N),
        .sh_usage = shmem.get_shared<int>(N),
        .continue_work = shmem.get_shared<int>()
    };
    *ctx.continue_work = 1;

    for (int i = 0; i < chiefs || i < consumers; i++) {
        if (i < chiefs)
            SPAWN(producer(ctx););
        if (i < consumers)
            SPAWN(client(ctx););
    }

    // usleep(2000);
    sleep(10);
    *ctx.continue_work = 0;

    LOG("CLOSING\nCLOSING\nCLOSING\n");

    // closing semaphores to stop processes
    CHECK(sem_close(ctx.empty), "sem-close");
    CHECK(sem_close(ctx.ready), "sem-close");
    CHECK(sem_unlink(SEM_READY_NAME), "sem-unlink");
    CHECK(sem_unlink(SEM_EMPTY_NAME), "sem-unlink");
    wait_for_all();

    CHECK(sem_unlink(SEM_MUTEX_NAME), "sem-unlink");

    return 0;
}
