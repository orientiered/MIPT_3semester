#include <cstdlib>
#include <stdio.h>

#include "../utils.hpp"
#include "../sysv_sem.hpp"

/*
sem spawn(M+W)
sem men(M)
sem women(W)
sem m_shower(0)
sem w_shower(0)
sem shower(N)
sem mutex(1)
sem m_priority(PRIO=2N)
sem w_priority(PRIO=2N)
men:
    p(spawn)
    z(spawn)

    {
    z(w_shower)
    v(m_shower)
    p(shower)

    p(m_priority)
    v(w_priority)
    }


    {
    v(m_shower)
    v(shower)
    p(men)
    }

women:
    p(spawn)
    z(spawn)

    {
    z(m_shower)
    v(w_shower)
    p(shower)

    p(w_priority)
    v(m_priority)
    }

    {
    v(shower)
    p(w_shower)
    p(women)
    }

*/

enum SEMS {
    SPAWN = 0,
    MSHOWER = 1,
    WSHOWER = 2,
    SHOWER = 3,
    MPRIO = 4,
    WPRIO = 5,
    MUTEX = 6,
    SMEN,
    SWOMEN
};

enum GENDER {
    MEN = 1,
    WOMEN = 2
};

static const char* const genders[] = {"men", "women"};

void log(int sem_id, unsigned type) {
    sem_wait(sem_id, MUTEX);

    int msh = 0, wsh = 0;
    CHECK(msh = semctl(sem_id, MSHOWER, GETVAL), "semctl error");
    CHECK(wsh = semctl(sem_id, WSHOWER, GETVAL), "semctl error");
    LOG("m:   %2d, w: %d\n", msh, wsh);
    LOG("In   shower: %s\n", genders[type-1]);

    sem_post(sem_id, MUTEX);
}

void fixer [[noreturn]] (int sem_id, enum SEMS sem_wait, enum SEMS sem_fix, unsigned value) {
    // waiting for zero
    CHECK(sem_wait_zero(sem_id, sem_wait), "semop error");
    // replacing with new value
    CHECK(sem_post_n(sem_id, sem_fix, value), "semop error");

    exit(0);
}


void bather [[noreturn]] (int sem_id, unsigned gender, unsigned capacity) {
    LOG("Ready to take bath: %u\n", gender);
    sem_wait(sem_id, SPAWN);
    sem_wait_zero(sem_id, SPAWN);
    // converge point

    switch(gender) {
        case MEN:
        {
            sembuf sops[] = {{MSHOWER, +1, SEM_UNDO}, {WSHOWER, 0, 0}, {SHOWER, -1, SEM_UNDO},
                             {MPRIO, -1, 0}, {WPRIO, 1, 0}, {SMEN, -1, 0}};
            CHECK(semop(sem_id, sops, 6), "semop error");
            break;
        }
        case WOMEN:
        {
            sembuf sops[] = {{WSHOWER, +1, SEM_UNDO}, {MSHOWER, 0, 0}, {SHOWER, -1, SEM_UNDO},
                             {MPRIO, +1, 0}, {WPRIO, -1, 0}, {SWOMEN, -1, 0}};
            CHECK(semop(sem_id, sops, 6), "semop error");
            break;
        }
        default:
            fprintf(stderr, "Unknown gender\n");
            exit(EXIT_FAILURE);
            break;
    }

    log(sem_id, gender);
    LOG("Left shower: %s\n", genders[gender-1]);

    exit(0);
}

int main(int argc, const char *argv[]) {
    unsigned N = 5;
    unsigned M = 7;
    unsigned W = 8;

    if (argc >= 4) {
        sscanf(argv[1], "%u", &N);
        sscanf(argv[2], "%u", &M);
        sscanf(argv[3], "%u", &W);
    }

    const int sem_count = 9;

    unsigned short init_values[] = {(unsigned short) (M+W), 0, 0, (unsigned short) N, (unsigned short) (2*N), 0, 1,
                                    (unsigned short) M, (unsigned short) W};
    int sem_id = sem_create(sem_count, init_values );
    CHECK(sem_id, "semget error");

    for (unsigned idx = 0; idx < M; idx++) {
        SPAWN(bather(sem_id, MEN, N););
    }

    for (unsigned idx = 0; idx < W; idx++) {
        SPAWN(bather(sem_id, WOMEN, N););
    }

    SPAWN(fixer(sem_id, SMEN, WPRIO, W););
    SPAWN(fixer(sem_id, SWOMEN, MPRIO, M););

    wait_for_all();

    semctl(sem_id, sem_count, IPC_RMID);
    return 0;
}
