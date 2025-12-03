#include "../utils.hpp"
#include <cctype>
#include <cstring>
#include <vector>
#include <set>
#include "sys/msg.h"

const int NPROC = 33;
const char * const text = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
const

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

};

struct init_vals {
    int mq;
    int mq2;
    int nproc;
    const char *text;
    const size_t text_len;
};

enum ACTION {
    HELLO,
    SING,
    TASK_END,
    END,
};

struct msg {
    long mtype;
    pid_t sender;
    int val;
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

const int LEADER_TYPE = 1;
const int FOLLOWER_TYPE = 2;


void worker(init_vals in) {
    info info;

    info.mymq = msgget(IPC_PRIVATE, IPC_CREAT | 0644);
    CHECK(info.mymq, "msgget 2");

    pid_t pid = getpid();
    std::set<int> mqs = {pid};
    // choosing leader
    msg hello = {pid, pid, info.mymq, HELLO};

    // leader is the process with highest mymq
    int turn = 0;
    while (true) {
        if (mqs.size() == in.nproc) break;

        CHECK(hello.send(in.mq), "snd");

        msg resp;
        CHECK(resp.rcv(in.mq, pid, MSG_EXCEPT), "rcv");

        if (resp.val > info.mymq) {
            info.role = FOLLOWER;
            send_mqs();


            break;
        } else {
            update_mqs();
        }

        turn++;
    }


    if (info.role == CANDIDATE) {
        info.role = LEADER;
        for (int i = 0; i < in.nproc - 1; i++) {
            msg resp;
            resp.rcv(in.mq2, 0, 0);
            mqs.push_back(resp.val);
        }
    }
    else {
        msg{pid, pid, info.mymq, HELLO}.send(in.mq2);
    }

    // leader
    if (info.role == LEADER) {
        for (int i = 0; i < in.text_len; i++) {
            int proc = char2idx(in.text[i]);
            if (proc < 0) continue;

            if (proc == 0) {
                // leader sings too
                printf("%c\n", in.text[i]);
                continue;
            }

            // sending char to sing
            struct msg msg = {LEADER_TYPE, getpid(), in.text[i], SING};
            CHECK(msg.send(mqs[proc]), "send");

            // receiving confirmation
            CHECK(msg.rcv(mqs[proc], LEADER_TYPE, MSG_EXCEPT), "rcv");
            if (msg.action != TASK_END) {
                PROC_LOG("Wrong confirmation message");
            }
        }

        // sending end msg
        struct msg msg = {LEADER_TYPE, getpid(), 0, END};
        for (int i = 1; i < mqs.size(); i++) {
            msg.send(mqs[i]);
        }

    } else {
        struct msg msg;
        while (true) {
            msg.rcv(info.mymq, FOLLOWER_TYPE, MSG_EXCEPT);

            if (msg.action == END) {
                break;
            } else if (msg.action == SING) {
                // putc(msg.val, stdout);
                printf("%c\n", msg.val);

                // confirm msg
                msg = {FOLLOWER_TYPE, getpid(), 0, TASK_END};
                msg.send(info.mymq);
            } else {
                PROC_LOG("Follower: unexpected action %d\n", msg.action);
            }
        }
    }

    PROC_LOG("%d finished\n", getpid());
    CHECK(msgctl(info.mymq, IPC_RMID, NULL), "mqueue remove 2");
}


int main(int argc, const char *argv[]) {

    int mq = msgget(IPC_PRIVATE, IPC_CREAT | 0644);
    CHECK(mq, "msgget");
    int mq2 = msgget(IPC_PRIVATE, IPC_CREAT | 0644);
    CHECK(mq2, "msgget");

    LOG("Spawning processes");

    init_vals in{mq, mq2, NPROC, text, strlen(text)};

    for (int i = 0; i < NPROC; i++) {
        SPAWN(worker(in););
    }

    wait_for_all();
    CHECK(msgctl(mq,  IPC_RMID, NULL), "mqueue remove");
    CHECK(msgctl(mq2, IPC_RMID, NULL), "mqueue remove");

    return 0;
}
