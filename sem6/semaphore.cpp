#include <cstdlib>
#include <stdio.h>

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>
/*
sem spawn(M+W)
sem m_shower(0)
sem w_shower(0)
sem shower(N)
sem mutex(1)
men:
    p(spawn)
    z(spawn)

    {
    z(w_shower)
    p(m_shower)
    v(shower)
    }


    {
    v(m_shower)
    p(shower)
    }

women:
    p(spawn)
    z(spawn)

    {
    z(m_shower)
    p(w_shower)
    v(shower)
    }

    {
    p(shower)
    v(w_shower)
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
    MUTEX = 4
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

void bather [[noreturn]] (int sem_id, unsigned gender, unsigned capacity) {
    fprintf(stderr, "Ready to take bath: %u\n", gender);
    sem_op_n(sem_id, -1, SPAWN);
    sem_z(sem_id, SPAWN);
    // converge point

    switch(gender) {
        case MEN:
        {
            sembuf sops[] = {{MSHOWER, +1, SEM_UNDO}, {WSHOWER, 0, 0}, {SHOWER, -1, SEM_UNDO}};
            CHECK(semop(sem_id, sops, 3), "semop error");
            break;
        }
        case WOMEN:
        {
            sembuf sops[] = {{WSHOWER, +1, SEM_UNDO}, {MSHOWER, 0, 0}, {SHOWER, -1, SEM_UNDO}};
            CHECK(semop(sem_id, sops, 3), "semop error");
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

    int sem_id = semget(IPC_PRIVATE, 5, IPC_CREAT | 0666 );
    CHECK(sem_id, "semget error");

    unsigned short init_values[] = {(unsigned short) (M+W), 0, 0, (unsigned short) N, 1};
    int code = semctl(sem_id, 5, SETALL, &init_values);
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

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}

    semctl(sem_id, 3, IPC_RMID);
    return 0;
}
