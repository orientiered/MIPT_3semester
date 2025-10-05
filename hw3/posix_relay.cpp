#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <mqueue.h>
#include <assert.h>

#include <sys/wait.h>

struct relay_msg {
    long type;
    long sender;
    char garbage[2048];
};

constexpr unsigned msg_size = 16;
constexpr int DEFAULT_PRIO = 1000;
#define CHECK(msg, ...) \
    do {\
        if ((__VA_ARGS__) < 0) {\
            perror(msg); exit(EXIT_FAILURE);\
        }\
    } while(0);


int mq_smart_receive(mqd_t mqdes, relay_msg *msg, long type) {
    assert(msg);

    do {
        unsigned prio = 0;
        CHECK("Receive error:", mq_receive(mqdes, (char *) msg, 21039812, NULL));
        // CHECK("Receive error:", mq_receive(mqdes, (char *) msg, 10924812, &prio));
        // printf("internal: recieved msg with type %ld\n", type);
        if (msg->type == type) break;
        else {
            prio = (prio > 0) ? prio - 1 : 0;
            CHECK("Send error:", mq_send(mqdes, (const char *) msg, msg_size, prio));
        }
    } while (true);

    return 0;
}

pid_t Fork()
{
    pid_t ret = fork();
    if (ret == -1) {
        perror("Fork error:");
    }

    return ret;
}

void judge(mqd_t msq_id, unsigned runners) {
    const int judge_id = runners+1;
    const int judge_reg = runners+2;
    relay_msg msg = {};

    int ready_runners = 0;
    // waiting for runners
    while (ready_runners < runners) {
        mq_smart_receive(msq_id, &msg, judge_reg);
        printf("Judge: registered runner %ld\n", msg.sender);
        ready_runners++;
    }

    printf("Judge: competition start\n");

    msg = {1, judge_id}; // passing baton to first runner
    CHECK("Send err:", mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO));
    printf("Judge: passed baton to runner 1\n");

    if (mq_smart_receive(msq_id, &msg, runners+1) < 0) {
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
    CHECK("Send error", mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO));

    // waiting for baton from judge or runner idx-1
    int rcv_status = mq_smart_receive(msq_id, &msg, idx);
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
    CHECK("Send err", mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO));
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

    const char *queue_name = "/relay_queue.mq";
    // umask(0);
    struct mq_attr attr = {.mq_flags = 0, .mq_maxmsg = 20, .mq_msgsize = 128, .mq_curmsgs = 0};
    mqd_t msq_id = mq_open(queue_name, O_RDWR | O_CREAT, 0666, NULL);
    if (msq_id < 0) {
        perror("Failed to create queue");
        exit(EXIT_FAILURE);
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

    mq_close(msq_id);
    mq_unlink(queue_name);

}
