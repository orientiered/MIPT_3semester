#include <cstdlib>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
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

#define CHECK(code, msg) if ((code) < 0) {perror(msg); exit(EXIT_FAILURE); }
void sem_op_n(int sem_id, short delta, unsigned short sem_num) {
    sembuf op = {sem_num, delta, 0};
    CHECK(semop(sem_id, &op, 1), "semop error");
}


void sem_z(int sem_id, unsigned short sem_num) {
    sembuf op = {sem_num, 0, 0};
    CHECK(semop(sem_id, &op, 1), "semop error");
}

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
    sem_op_n(sem_id, -1, MUTEX);

    int msh = 0, wsh = 0;
    CHECK(msh = semctl(sem_id, MSHOWER, GETVAL), "semctl error");
    CHECK(wsh = semctl(sem_id, WSHOWER, GETVAL), "semctl error");
    fprintf(stderr, "m:   %2d, w: %d\n", msh, wsh);
    fprintf(stderr, "In   shower: %s\n", genders[type-1]);
    sem_op_n(sem_id, 1, MUTEX);
}

void fixer [[noreturn]] (int sem_id, enum SEMS sem_wait, enum SEMS sem_fix, unsigned value) {
    sembuf sops[] = {{(unsigned short) sem_wait, 0, 0}};
    // waiting for zero
    CHECK(semop(sem_id, sops, 1), "semop error");

    // replacing with new value
    sops[0].sem_num = sem_fix;
    sops[0].sem_op = value;
    CHECK(semop(sem_id, sops, 1), "semop error");

    exit(0);
}


void bather [[noreturn]] (int sem_id, unsigned gender, unsigned capacity) {
    fprintf(stderr, "Ready to take bath: %u\n", gender);
    sem_op_n(sem_id, -1, SPAWN);
    sem_z(sem_id, SPAWN);
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
    fprintf(stderr, "Left shower: %s\n", genders[gender-1]);

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

    int sem_id = semget(IPC_PRIVATE, 9, IPC_CREAT | 0666 );
    CHECK(sem_id, "semget error");

    unsigned short init_values[] = {(unsigned short) (M+W), 0, 0, (unsigned short) N, (unsigned short) (2*N), 0, 1,
                                    (unsigned short) M, (unsigned short) W};
    int code = semctl(sem_id, 9, SETALL, &init_values);
    CHECK(code, "semctl error");


    for (unsigned idx = 0; idx < M; idx++) {
        pid_t pid = fork();
        CHECK(pid, "Fork error");
        if (pid == 0) {
            bather(sem_id, MEN, N);
        }
    }

    for (unsigned idx = 0; idx < W; idx++) {
        pid_t pid = fork();
        CHECK(pid, "Fork error");
        if (pid == 0) {
            bather(sem_id, WOMEN, N);
        }
    }

    pid_t pid = fork();
    CHECK(pid, "Fork error");
    if (pid == 0) fixer(sem_id, SMEN, WPRIO, W);

    pid = fork();
    CHECK(pid, "Fork error");
    if (pid == 0) fixer(sem_id, SWOMEN, MPRIO, M);

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}

    semctl(sem_id, 7, IPC_RMID);
    return 0;
}
