#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>

struct context {
    int sem_details;
    int N;
};

/*
Было в контрольной:
sem details = 2;
wrench guy:
    p(details)
    // put nut

screw guy:
    // put screw
    z(details)
    // put new detail
    details = 2

Ошибка: рабочий с отвёрткой может начать менять деталь, когда гаечники еще не закрутили гайки
*/

/*
Правильное решение
sem details = 2;
sem working = 2;
wrench guy:
    p(details)
    // put nut
    p(working)

screw guy:
    // put screw
    z(working)
    // put new detail
    working = 2
    details = 2

*/
#define CHECK(expr, msg) if ((long long)(expr) < 0) {perror(msg); exit(EXIT_FAILURE); }

enum sem_names {
    DETAILS = 0,
    WORKING = 1
};
void wrench [[noreturn]] (volatile context ctx) {
    while (true) {
        sembuf op = {DETAILS, -1, 0};
        int status = semop(ctx.sem_details, &op, 1);
        if (status < 0) {
            // sem was removed
            break;
        }
        fprintf(stderr, "Wrench worker%d: put nut on detail\n", getpid()%100);

        op = {WORKING, -1, 0};
        status = semop(ctx.sem_details, &op, 1);

    }

    fprintf(stderr, "wrench %d:Finished work\n", getpid()%100);

    exit(0);
}

void screw [[noreturn]] (volatile context ctx) {
    while (ctx.N > 0) {
        fprintf(stderr, "Screw worker: put screw\n");

        sembuf op = {WORKING, 0, 0};
        CHECK(semop(ctx.sem_details, &op, 1), "semop1");

        fprintf(stderr, "Screw worker: removed finished detail and put new one\n");

        op = {WORKING, 2, 0};
        CHECK(semop(ctx.sem_details, &op, 1), "semop2");
        op = {DETAILS, 2, 0};
        CHECK(semop(ctx.sem_details, &op, 1), "semop2");

        ctx.N--;
    }

    fprintf(stderr, "screw %d:Finished work\n", getpid()%100);

    semctl(ctx.sem_details, 2, IPC_RMID);
    exit(0);
}


#define SPAWN(__VA_ARGS__) \
    do {                            \
        pid_t pid = fork();         \
        CHECK(pid, "Fork error");   \
        if (pid == 0) { __VA_ARGS__ } \
    } while(0)

int main(int argc, const char *argv[]) {
    context ctx = {
        .sem_details = semget(IPC_PRIVATE, 2, IPC_CREAT | 0666 ),
        .N = 10
    };

    unsigned short vals[] = {2, 2};
    CHECK(semctl(ctx.sem_details, 2, SETALL, &vals), "set error");

    SPAWN(screw(ctx););
    SPAWN(screw(ctx););
    SPAWN(wrench(ctx););

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}

    return 0;
}
