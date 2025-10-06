#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <mqueue.h>
#include <assert.h>

#include <sys/time.h>
#include <sys/wait.h>
#include <utility>
#include <cmath>

struct time_span {
    long minutes;
    double seconds;
};

std::pair<double, double> runningSTD(double value, int getResult) {
    //function to calculate standard deviation of some value
    //constructed to make calculations online, so static variables
    static std::pair<double, double> result = {};
    static unsigned measureCnt = 0;     //number of values
    static double totalValue = 0;       //sum of value
    static double totalSqrValue = 0;    //sum of value^2
    // getResult > 1 --> calculate meanValue and std and return it
    // getResult = 0 --> store current value
    // getResult < 0 --> reset stored values
    if (getResult > 0) {
        if (measureCnt > 1) {
            result.first = totalValue / measureCnt;
            //printf("%g %g %u\n", totalSqrValue, totalValue, measureCnt);
            result.second = sqrt(totalSqrValue / measureCnt - result.first*result.first) / sqrt(measureCnt - 1);
        }
        return result;
    } else if (getResult == 0) {
        measureCnt++;
        totalValue += value;
        totalSqrValue += value*value;
    } else {
        measureCnt = 0;
        totalValue = totalSqrValue = 0;
    }
    return result;
}

struct Timer {
    struct timeval start_, end_;
    void start() {
        gettimeofday(&start_, NULL);
    }

    time_span stop() {
        gettimeofday(&end_, NULL);
        long int time_usec = end_.tv_sec * 1e6 + end_.tv_usec -start_.tv_sec * 1e6 - start_.tv_usec;
        long int minutes = time_usec / (1e6 * 60);
        time_usec -= minutes * (1e6 * 60);
        return {minutes, (double) time_usec / 1e6};
    }
};

struct relay_msg {
    long type;
    long sender;
};

constexpr unsigned msg_size = sizeof(relay_msg);
constexpr int DEFAULT_PRIO = 4096;
constexpr long PARENT_TYPE = 1000000;

#define CHECK(msg, ...) \
    do {\
        if ((__VA_ARGS__) < 0) {\
            perror(msg); exit(EXIT_FAILURE);\
        }\
    } while(0);


static long resend_counter = 0;

int mq_smart_receive(mqd_t mqdes, relay_msg *msg, long type) {
    assert(msg);

    do {
        unsigned prio = 0;
        CHECK("Receive error:", mq_receive(mqdes, (char *) msg, msg_size, &prio));
        if (type == 0) break;
        else if (type < 0 && msg->type <= (-type) ) break;
        else if (msg->type == type) break;
        else {
            resend_counter++;
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

void judge [[noreturn]] (mqd_t msq_id, unsigned runners, unsigned laps);
void runner [[noreturn]] (int msq_id, unsigned idx, unsigned runners, unsigned laps);

void judge [[noreturn]] (mqd_t msq_id, unsigned runners, unsigned laps) {
    const long judge_id = runners+1;
    const long judge_reg = runners+2;
    relay_msg msg = {};

    unsigned ready_runners = 0;
    // waiting for runners
    while (ready_runners < runners) {
        mq_smart_receive(msq_id, &msg, judge_reg);
        printf("Judge: registered runner %ld\n", msg.sender);
        ready_runners++;
    }

    printf("Judge: competition start\n");

    Timer total_time;
    total_time.start();
    runningSTD(0, -1);

    for (unsigned lap = 0; lap < laps; lap++) {
        Timer timer;
        timer.start();

        msg = {1, judge_id}; // passing baton to first runner
        CHECK("Send err:", mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO));
        printf("Judge: passed baton to runner 1\n");

        if (mq_smart_receive(msq_id, &msg, runners+1) < 0) {
            perror("Failed to rcv msg");
        }

        printf("Judge: recieved baton from runner %ld\n", msg.sender);
        time_span span = timer.stop();
        runningSTD(span.minutes*60 + span.seconds, 0);
        printf("Judge: lap %u/%u finished in %ldm%.3f s\n", lap+1, laps, span.minutes, span.seconds);
    }

    time_span total = total_time.stop();
    printf("Judge: race finished in %ldm%.3f s\n", total.minutes, total.seconds);

    printf("Average time is %.2f +- %.2f sec\n", runningSTD(0, 1).first, runningSTD(0, 1).second);
    // statistics

    msg = {PARENT_TYPE, resend_counter};
    mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO);

    exit(0);
}


void runner [[noreturn]] (int msq_id, unsigned idx, unsigned runners, unsigned laps) {
    const long judge_id = runners+1;

    relay_msg msg = {judge_id + 1, idx};
    // sending ready msg to judge registration port
    printf("Runner %u: ready\n", idx);
    CHECK("Send error", mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO));

    for (unsigned lap = 0; lap < laps; lap++) {
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
    }

    msg = {PARENT_TYPE, resend_counter};
    mq_send(msq_id, (const char *) &msg, msg_size, DEFAULT_PRIO);

    exit(0);
}

int main(int argc, const char *argv[]) {
    unsigned N_runners = 15, N_laps = 1;

    if (argc > 1) {
        sscanf(argv[1], "%u", &N_runners);
    }

    if (argc > 2) {
        sscanf(argv[2], "%u", &N_laps);
    }

    // Opening message queue
    const char *queue_name = "/relay_queue.mq";
    struct mq_attr attr = {.mq_flags = 0, .mq_maxmsg = 10, .mq_msgsize = msg_size, .mq_curmsgs = 0};
    mqd_t msq_id = mq_open(queue_name, O_RDWR | O_CREAT, 0666, &attr);
    if (msq_id < 0) {
        perror("Failed to create queue");
        exit(EXIT_FAILURE);
    }

    // Launching judge
    pid_t judge_pid = Fork();
    if (judge_pid == 0) {
        judge(msq_id, N_runners, N_laps);
    }

    // Launching runners
    for (unsigned i = 0; i < N_runners; i++) {
        pid_t runner_pid = Fork();
        if (runner_pid == 0) {
            runner(msq_id, i+1, N_runners, N_laps);
        }
    }

    // Counting total message resends
    long total_resends = resend_counter;
    for (unsigned i = 0; i < N_runners+1; i++) {
        relay_msg msg = {};
        mq_smart_receive(msq_id, &msg, PARENT_TYPE);
        total_resends += msg.sender;
    }
    printf("total resends = %ld\n", total_resends);

    // Waiting for child processes
    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {}

    // Closing queue
    mq_close(msq_id);
    mq_unlink(queue_name);

    return 0;
}
