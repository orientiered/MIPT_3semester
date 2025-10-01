#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

struct relay_msg {
    long type;
    long sender;
};

const size_t msg_size = sizeof(relay_msg) - sizeof(long);

pid_t Fork()
{
    pid_t ret = fork();
    if (ret == -1) {
        perror("Fork error:");
    }

    return ret;
}

void judge(int msq_id, unsigned runners) {
    const int judge_id = runners+1;
    const int judge_reg = runners+2;
    relay_msg msg = {};

    int ready_runners = 0;
    // waiting for runners
    while (ready_runners < runners) {
        msgrcv(msq_id, &msg, msg_size, judge_reg, 0);
        printf("Judge: registered runner %ld\n", msg.sender);
        ready_runners++;
    }

    printf("Judge: competition start\n");

    msg = {1, judge_id}; // passing baton to first runner
    msgsnd(msq_id, &msg, msg_size, 0);
    printf("Judge: passed baton to runner 1\n");

    if (msgrcv(msq_id, &msg, msg_size, runners+1, 0) < 0) {
        perror("Failed to rcv msg");
    }

    printf("Judge: recieved baton from runner %ld\n", msg.sender);
    printf("Judge: race finished\n");

    exit(0);
}

void runner(int msq_id, unsigned idx, unsigned runners) {
    const int judge_id = runners+1;

    relay_msg msg = {judge_id + 1, idx};
    // sending ready msg to judge registration port
    printf("Runner %u: ready\n", idx);
    msgsnd(msq_id, &msg, msg_size, 0);

    // waiting for baton from judge or runner idx-1
    int rcv_status = msgrcv(msq_id, &msg, msg_size, idx, 0);
    if (rcv_status < 0) {
        perror("Failed to recieve msg");
    }
    printf("Runner %u: recieved baton from ", idx);

    if (msg.sender == judge_id) {
        printf("judge\n");
    } else {
        printf("runner %ld\n", msg.sender);
    }

    msg = {idx+1, idx};
    // sending baton to next runner
    printf("Runner %u: passed baton to ", idx);
    msgsnd(msq_id, &msg, msg_size, 0);
    if (idx == runners) {
        printf("judge\n");
    } else {
        printf("runner %ld\n", msg.type);
    }

    exit(0);
}

int main(int argc, const char *argv[]) {
    int N = 15;

    if (argc > 1) {
        sscanf(argv[1], "%d", &N);
    }

    int msq_id = msgget(IPC_PRIVATE, IPC_CREAT | 0666);
    if (msq_id < 0) {
        perror("Failed to create msg queue");
    }

    pid_t judge_pid = Fork();
    if (judge_pid == 0) {
        judge(msq_id, N);
    }

    for (int i = 0; i < N; i++) {
        pid_t runner_pid = Fork();
        if (runner_pid == 0) {
            runner(msq_id, i+1, N);
        }
    }

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {
        // printf("Terminated child process %d by %d\n", closed_pid, getpid());
    }

    msgctl(msq_id, IPC_RMID, NULL);

}
