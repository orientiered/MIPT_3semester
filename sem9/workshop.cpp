#include "../sysv_sem.hpp"

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

enum sem_names {
    DETAILS = 0,
    WORKING = 1
};

void wrench [[noreturn]] (volatile context ctx) {
    while (true) {
        if (sem_wait(ctx.sem_details, DETAILS) < 0) {
            // sem was removed
            break;
        }
        PROC_LOG("Wrench worker: put nut on detail\n");

        sem_wait(ctx.sem_details, WORKING);
    }

    PROC_LOG("Wrench: Finished work\n");

    exit(0);
}

void screw [[noreturn]] (volatile context ctx) {
    while (ctx.N > 0) {
        PROC_LOG("Screw worker: put screw\n");

        sem_wait_zero(ctx.sem_details, WORKING);

        PROC_LOG("Screw worker: removed finished detail and put new one\n");

        sem_post_n(ctx.sem_details, WORKING, 2);
        sem_post_n(ctx.sem_details, DETAILS, 2);

        ctx.N--;
    }

    PROC_LOG("Screw: finished work\n");

    semctl(ctx.sem_details, 2, IPC_RMID);
    exit(0);
}

int main(int argc, const char *argv[]) {
    unsigned short vals[] = {2, 2};
    context ctx = {
        .sem_details = sem_create(2, vals),
        .N = 10
    };

    SPAWN(screw(ctx););
    SPAWN(screw(ctx););
    SPAWN(wrench(ctx););

    wait_for_all();
    return 0;
}
