#pragma once

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "utils.hpp"

inline int sem_create(unsigned sem_count, unsigned short *vals) {
    int sem_id = semget(IPC_PRIVATE, sem_count, IPC_CREAT | 0666);
    SOFT_CHECK(sem_id, "semaphore creation");
    SOFT_CHECK(semctl(sem_id, sem_count, SETALL, vals), "semaphore init");
    return sem_id;
}

inline int sem_wait(int sem_id, unsigned short sem_num) {
    sembuf op = {sem_num, -1, 0};
    return semop(sem_id, &op, 1);
}

inline int sem_post(int sem_id, unsigned short sem_num) {
    sembuf op = {sem_num, 1, 0};
    return semop(sem_id, &op, 1);
}

inline int sem_post_n(int sem_id, unsigned short sem_num, short n) {
    sembuf op = {sem_num, n, 0};
    return semop(sem_id, &op, 1);
}

inline int sem_wait_zero(int sem_id, unsigned short sem_num) {
    sembuf op = {sem_num, 0, 0};
    return semop(sem_id, &op, 1);
}

inline int sem_get_val(int sem_id, unsigned short sem_num) {
    return semctl(sem_id, sem_num, GETVAL);
}
