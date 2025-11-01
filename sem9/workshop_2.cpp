#include "sysv_sem.hpp"
#include "utils.hpp"

struct context {
    int sem_details;
    int N;
};

/*
sem A = 2, B = 1
sem total = 3
sem mtx = 1

def change_detail:
    p(mutex)
    if (total == 0) {
        A = 2;
        B = 1;
        total = 3;
    }
    v(mutex)


wrench:
    p(A)

    p(total)

    change_detail()

screw:
    p(B)

    p(total)
    change_detail()

*/

enum sem_names {
    A = 0,
    B = 1,
    TOTAL = 2,
    MTX = 3
};

const int sem_count = 4;

void change_detail(context ctx) {
    sem_wait(ctx.sem_details, MTX);

    if (sem_get_val(ctx.sem_details, TOTAL) == 0) {
        PROC_LOG(COL_GREEN "Changed detail\n" COL_RESET);
        sem_post_n(ctx.sem_details, A, 2);
        sem_post_n(ctx.sem_details, B, 1);
        sem_post_n(ctx.sem_details, TOTAL, 3);
    }

    sem_post(ctx.sem_details, MTX);
}

void wrench [[noreturn]] (context ctx) {
    while (true) {
        if (sem_wait(ctx.sem_details, A) < 0) {
            // sem was removed
            break;
        }
        PROC_LOG("Wrench: put nut on detail\n");

        sem_wait(ctx.sem_details, TOTAL);

        change_detail(ctx);
    }

    PROC_LOG("Wrench:Finished work\n");

    exit(0);
}

void screw [[noreturn]] (context ctx) {
    while (ctx.N--) {
        if (sem_wait(ctx.sem_details, B) < 0) {
            // sem was removed
            break;
        }
        PROC_LOG("Screw: put screw on detail\n");

        sem_wait(ctx.sem_details, TOTAL);

        change_detail(ctx);
    }

    PROC_LOG("Screw: Finished work\n");
    semctl(ctx.sem_details, sem_count, IPC_RMID);
    exit(0);
}

int main(int argc, const char *argv[]) {
    unsigned short vals[] = {2, 1, 3, 1};
    static_assert(sizeof(vals)/sizeof(unsigned short) == sem_count);

    context ctx = {
        .sem_details = sem_create(sem_count, vals),
        .N = 10
    };

    SPAWN(screw(ctx););
    SPAWN(screw(ctx););
    SPAWN(wrench(ctx););

    wait_for_all();
    return 0;
}
