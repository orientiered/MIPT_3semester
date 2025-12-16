#include "../utils.hpp"
#include <cctype>
#include <cstring>
#include <vector>
#include <set>
#include "sys/msg.h"

const int NPROC = 33;
const char * const text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";

enum ROLE {
    FOLLOWER,
    CANDIDATE,
    LEADER
};

struct info {
    int role = CANDIDATE;
    char my_letter = 0;
    char *cur = nullptr;
    int mymq = 0;
    int pid;

    std::set<int> mqs;

};

struct init_vals {
    int mq;
    int nproc;
    const char *text;
    const size_t text_len;
};

enum ACTION {
    HELLO,
    SING,
    SEND_MQS,
    REQUEST_MQS,
    TASK_END,
    END,
};

struct msg {
    long mtype;
    pid_t sender;
    long val;
    int action;

    int send(int mq, int flg = 0);
    int rcv(int mq, int type, int flg);
};

const size_t msg_sz = sizeof(msg) - sizeof(long);

int msg::send(int mq, int flg) {
    return msgsnd(mq, this, msg_sz, flg);
}

int msg::rcv(int mq, int type, int flg) {
    return msgrcv(mq, this, msg_sz, type, flg);
}


int char2idx(char c) {
    if (c == ' ') return 0;
    if (c == ',') return 1;
    if (c == '.') return 2;
    if (isalpha(c)) return 2 + tolower(c) - 'a';

    return -1;
}


enum MTYPE {
    LEADER_TYPE = 1,
    FOLLOWER_TYPE,
    MQS_TYPE,
    REQUEST_TYPE
};

void send_mqs(info& info, int recipient) {
    msg msg = {MQS_TYPE, info.pid, (long) info.mqs.size(), SEND_MQS};
    CHECK(msg.send(recipient), "send update len");

    for (int mq: info.mqs) {
        msg.val = mq;
        CHECK(msg.send(recipient), "send update");
    }
}

void update_mqs(info& info, int loser_mq) {
    msg msg = {REQUEST_TYPE, info.pid, info.mymq, REQUEST_MQS};
    msg.send(loser_mq);

    CHECK(msg.rcv(info.mymq, MQS_TYPE, 0), "rcv update len");

    if (msg.action != SEND_MQS) {
        LOG("Bad msg action (expected send mqs)\n");
    }

    for (int idx = msg.val; idx > 0; idx--) {
        CHECK(msg.rcv(info.mymq, MQS_TYPE, 0), "rcv update");
        info.mqs.insert(msg.val);
    }

    PROC_LOG("MQS update success: sz = %ld\n", info.mqs.size());

}

void leader(info& info, init_vals& in);
void follower(info& info, init_vals& in);

void worker(init_vals in) {
    info info;
    info.pid = getpid();

    info.mymq = msgget(IPC_PRIVATE, IPC_CREAT | 0644);
    CHECK(info.mymq, "msgget 2");
    PROC_LOG("mymq = %d\n", info.mymq);

    info.mqs.insert(info.mymq);


    // choosing leader
    msg hello = {info.pid, info.pid, info.mymq, HELLO};

    // leader is the process with highest own message queue id
    // everyone sends his mqid to common queue and receives exactly one message from other process (MSG_EXCEPT)
    // then
    while (true) {
        // term++;
        // PROC_LOG("Term %d\n", term);

        if (info.mqs.size() == in.nproc) break;

        // Отправляем в общий канал сообщение со своим id очереди
        CHECK(hello.send(in.mq), "snd");

        msg resp;
        if (info.mqs.size() - in.nproc <= 2) {
            PROC_LOG("Send hello message, mqs size = %ld\n", info.mqs.size());
        }

        // читаем сообщение с id очереди ДРУГОГО процесса
        CHECK(resp.rcv(in.mq, info.pid, MSG_EXCEPT), "rcv");

        // Если у полученного сообщений больший id, то мы не можем быть лидером
        if (resp.val > info.mymq) {
            PROC_LOG("Removed from leader candidates; mqs size = %zu\n", info.mqs.size());
            info.role = FOLLOWER;
            break;
        } else {
        // Иначе мы можем быть лидером и нужно обновить набор процессов, с которыми познакомились
        // Отправляем запрос на обновление процессу, которого мы обогнали
        // Он точно не лидер, поэтому через какое-то время он станет follower и ответит на запрос
            update_mqs(info, resp.val);
        }

    }

    if (info.role == CANDIDATE) {
        info.role = LEADER;
    }

    // leader
    if (info.role == LEADER) {
        leader(info, in);
    } else {
        follower(info, in);
    }

    PROC_LOG("%d finished\n", getpid());
    CHECK(msgctl(info.mymq, IPC_RMID, NULL), "mqueue remove 2");

    exit(0);
}

void leader(info& info, init_vals& in) {
    PROC_LOG("Leader found\n");
    std::vector<int> mqs(info.mqs.begin(), info.mqs.end());

    for (int i = 0; i < in.text_len; i++) {
        int proc = char2idx(in.text[i]);
        if (proc < 0) continue;

        if (mqs[proc] == info.mymq) {
            // leader sings too
            printf("%c\n", in.text[i]);
            continue;
        }

        // sending char to sing
        struct msg msg = {LEADER_TYPE, info.mymq, in.text[i], SING};
        // LOG("sending %c to mqs[%d] = %d\n", in.text[i], proc, mqs[proc]);
        CHECK(msg.send(mqs[proc]), "send");

        // receiving confirmation
        CHECK(msg.rcv(info.mymq, FOLLOWER_TYPE, 0), "rcv");
        if (msg.action != TASK_END) {
            PROC_LOG("Wrong confirmation message");
        }
    }

    // sending end msg
    struct msg msg = {LEADER_TYPE, info.pid, 0, END};
    for (int i = 0; i < mqs.size(); i++) {
        msg.send(mqs[i]);
    }

}

void follower(info& info, init_vals& in) {
    msg msg;
    // PROC_LOG("Follower init\n");
    while (true) {
        msg.rcv(info.mymq, 0, 0);

        if (msg.action == REQUEST_MQS) {
            PROC_LOG("mqs request from %ld\n", msg.val);
            send_mqs(info, msg.val);
        } else
        if (msg.action == END) {
            return;
        } else if (msg.action == SING) {
            // putc(msg.val, stdout);
            printf("%c\n", msg.val);

            // confirm msg
            int leader = msg.sender;
            msg = {FOLLOWER_TYPE, getpid(), 0, TASK_END};
            msg.send(leader);
        } else {
            PROC_LOG("Follower: unexpected action %d\n", msg.action);
        }
    }
}

int main(int argc, const char *argv[]) {

    int mq = msgget(IPC_PRIVATE, IPC_CREAT | 0644);
    CHECK(mq, "msgget");

    LOG("Spawning processes\n");

    init_vals in{mq, NPROC, text, strlen(text)};

    for (int i = 0; i < NPROC; i++) {
        SPAWN(worker(in););
    }

    wait_for_all();
    CHECK(msgctl(mq,  IPC_RMID, NULL), "mqueue remove");

    return 0;
}
