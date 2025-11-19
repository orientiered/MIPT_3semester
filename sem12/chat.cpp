#include "../utils.hpp"
#include <cstring>
#include <signal.h>

#include <sys/ipc.h>
#include <sys/shm.h>

const size_t MAX_USERS = 256;

const size_t MAX_MSG_SIZE = 256;
#define MSG_FMT " %255[^\n]" // must be MAX_MSG_SIZE-1
#define CMD_FMT "%255s " // read string and spaces after it
const size_t QUEUE_BLOCK_SIZE = MAX_MSG_SIZE + 20;
const size_t MSG_QUEUE_LEN = 32;
const size_t MSG_QUEUE_SIZE = QUEUE_BLOCK_SIZE * MSG_QUEUE_LEN;

//TODO: it should work as monitor
struct chat_info {
    pid_t users[MAX_USERS];
    size_t user_cnt = 0;

    char msg_queue[MSG_QUEUE_SIZE];
    int  msg_idx = 0;
};


const int SIG_JOIN_REQUEST = SIGRTMIN;
const int SIG_JOIN_HANDLE  = SIGRTMIN + 1;

const int SIG_MSG = SIGRTMIN + 2;

void join_handler(int signum, siginfo_t *siginfo, void *);

void exit_handler(int signum);

void msg_handler(int signum, siginfo_t *siginfo, void *);


enum ChatStatus {
    BAD_PID    =  2, // pid not registered in users
    BAD_FORMAT =  1, // wrong format for /tell command
    SUCCESS    =  0,
    CHAT_ERR   = -1,
    SCANF_FAIL = -2,
    QUIT_CMD   = -3,
    USER_OVERFLOW = -4
};

struct chat {
    chat_info *info = nullptr;
    int my_id = -1;
    int shm_fd = -1;

    char msg_buf[256] = {};
    static const size_t msg_buf_size = 255;

    char *line = nullptr;
    char *line_ptr = nullptr;
    size_t line_size = 0;

    int add_to_users() {
        // TODO: mutex_lock
        if (info->user_cnt >= MAX_USERS) {
            // TODO: mutex_unlock
            return USER_OVERFLOW;
        }

        for (int i = 0; i < MAX_USERS; i++) {
            if (info->users[i] == 0) {
                info->users[i] = getpid();
                my_id = i;
                info->user_cnt++;
                PROC_LOG("Current user count %zu\n", info->user_cnt);
                // TODO: mutex_unlock
                return SUCCESS;
            }
        }

        // TODO: mutex_unlock
        return CHAT_ERR;
    }

    int leave_from_users() {

        if (my_id < 0) return SUCCESS;

        // TODO: mutex_lock
        info->users[my_id] = 0;
        info->user_cnt--;

        my_id = -1;
        PROC_LOG("Leave: current user count %zu\n", info->user_cnt);

        // TODO: mutex_unlock
        return SUCCESS;
    }

    // not async-safe
    int delete_from_users(int pid) {
        PROC_LOG("Deleting user with pid %d\n", pid);

        for (int i = 0; i < MAX_USERS; i++) {
            if (info->users[i] == pid) {
                info->users[i] = 0;
                if (i == my_id)
                    my_id = -1;
                info->user_cnt--;
                PROC_LOG("Current user count %zu\n", info->user_cnt);
                return SUCCESS;
            }
        }

        return SUCCESS;
    }


    int join(pid_t join_pid) {
        if (join_pid < 0) {
            PROC_LOG("Creating shmem\n");

            shm_fd = shmget(IPC_PRIVATE, sizeof(chat_info), IPC_CREAT | 0666);
            CHECK(shm_fd, "Create shm");
            info = (chat_info *) shmat(shm_fd, NULL, 0);
            CHECK(info, "Attach shm");

            add_to_users();

            PROC_LOG("Basic init finished\n");

            return 0;
        }

        PROC_LOG("Sending join request\n");
        sigqueue(join_pid, SIG_JOIN_REQUEST, {0});

        siginfo_t join_info = {};
        sigset_t set = {};
        CHECK(sigaddset(&set, SIG_JOIN_HANDLE), "addset");

        PROC_LOG("Waiting for join answer\n");
        CHECK(sigwaitinfo(&set, &join_info), "join wait handle");

        shm_fd = join_info.si_value.sival_int;
        info = (chat_info *) shmat(shm_fd, NULL, 0);
        CHECK(info, "Attach shm");

        add_to_users();

        PROC_LOG("Attached to chat with process %d\n", join_info.si_pid);

        return 0;
    }

    // not async-safe
    int send_msg(pid_t pid, int msg_idx, bool check_pid = false) {
        PROC_LOG("Formed msg: '%s'\n", &info->msg_queue[msg_idx*QUEUE_BLOCK_SIZE]);

        if (check_pid) {
            bool found_user = false;
            for (int i = 0; i < MAX_USERS; i++) {
                if (info->users[i] == pid) {
                    found_user = true;
                    break;
                }
            }

            if (!found_user) {
                fprintf(stderr, "Bad pid\n");
                return BAD_PID;
            }
        }

        // TODO: check error code and remove unexistent users
        int status = sigqueue(pid, SIG_MSG, {msg_idx});

        if (status < 0 && errno == ESRCH) {
            delete_from_users(pid);
        } else SOFT_CHECK(status, "sigqueue");

        return 0;
    }

    // not async-safe
    int write_msg(const char *buf) {
        int idx = info->msg_idx;
        info->msg_idx = (info->msg_idx + 1) % MSG_QUEUE_LEN;

        strncpy(info->msg_queue + idx*QUEUE_BLOCK_SIZE, buf, QUEUE_BLOCK_SIZE);
        return idx;
    }

    int handle_tell() {
        int pid = 0;
        int symbols = 0;
        if (sscanf(line_ptr, "%d %n", &pid, &symbols) < 1) {
            return BAD_FORMAT;
        }
        line_ptr+=symbols;

        char buf[QUEUE_BLOCK_SIZE] = {};
        sprintf(buf, "[%d told you]: %s\n", getpid(), line_ptr);

        // TODO: mutex_lock
        int msg_idx = write_msg(buf);
        int status = send_msg(pid, msg_idx, true);
        // TODO: mutex_unlock
        return status;
    }

    int handle_say() {
        char buf[QUEUE_BLOCK_SIZE] = {};
        sprintf(buf, "%d: %s\n", getpid(), line_ptr);

        // TODO: mutex_lock
        int msg_idx = write_msg(buf);
        PROC_LOG("msg = '%s', msg idx = %d\n", buf, msg_idx);

        int status = SUCCESS;

        for (int i = 0; i < MAX_USERS; i++) {
            if (i == my_id) continue; // don't send to sender
            if (info->users[i] == 0) continue; // don't send to empty processes

            int status = send_msg(info->users[i], msg_idx);
            if (status < 0) {
                break;
            }
        }

        // TODO: mutex_unlock
        return status;
    }

    int handle_stdin() {
        //TODO: long messages handling
        int symbols = 0;
        if ((symbols = getline(&line, &line_size, stdin)) < 0) {
            return SCANF_FAIL;
        }

        line_ptr = line;

        // line started with /
        if (line[0] == '/') {
            sscanf(line_ptr, "%255s %n", msg_buf, &symbols); line_ptr += symbols;

            // if (scanned <= 0) {
            //     PROC_LOG("scanf fail\n");
            //     return SCANF_FAIL;
            // }

            if (strcmp(msg_buf, "/bye") == 0) {
                return QUIT_CMD;
            } else if (strcmp(msg_buf, "/tell") == 0) {
                return handle_tell();
            } else if (strcmp(msg_buf, "/say") == 0) {
                return handle_say();
            } else {
                printf("Unknown command\n");
                return 0;
            }
        }

        return handle_say();
    }

    int destroy() {
        free(line); line = nullptr;
        if (!info) {
            return 0;
        }

        //TODO: mutex_lock
        // last user closes shmem

        int users_left = info->user_cnt;
        leave_from_users();
        CHECK(shmdt(info), "shm detach");
        info = nullptr;

        //TODO: mutex_unlock
        //TODO: mutex delete
        if (users_left == 1) {
            CHECK(shmctl(shm_fd, IPC_RMID, NULL), "shm unlink");
        }

        shm_fd = -1;

        return 0;
    }
};

/* ===========================    Global chat    ==================== */

sigset_t empty_set;
sigset_t normal_set;

chat chat;

/* =========================== SIGNAL handlers ==================== */

void join_handler(int signum, siginfo_t *siginfo, void *) {
    if (signum == SIG_JOIN_HANDLE) {
        return;
    }

    pid_t sender_pid = siginfo->si_pid;
    sigqueue(sender_pid, SIG_JOIN_HANDLE, {chat.shm_fd});
}

void exit_handler(int signum) {
    if (signum == SIGINT) {
        chat.destroy();
    }

    _exit(1);
}

void msg_handler(int signum, siginfo_t *siginfo, void *) {
    if (signum == SIG_MSG) {
        const char *msg = chat.info->msg_queue + siginfo->si_value.sival_int * QUEUE_BLOCK_SIZE;
        const size_t msg_size = strlen(msg);
        write(1, msg, msg_size);
    }
}

void init_signals();

int main(int argc, const char *argv[]) {

    init_signals();

    pid_t join_pid = -1;
    if (argc > 1) {
        sscanf(argv[1], "%d", &join_pid);
    }

    printf("PID: %d\n", getpid());

    chat.join(join_pid);

    while (true) {
        if (chat.handle_stdin() < 0) {
            PROC_LOG("Handle stdin returned < 0\n");
            break;
        }
    }

    chat.destroy();
    return 0;
}

void init_signals() {
    sigemptyset(&empty_set);
    normal_set = empty_set;
    CHECK(sigaddset(&normal_set, SIGINT), "add sigint");

    struct sigaction action = {
        NULL,
        empty_set,
        SA_SIGINFO | SA_RESTART,
        NULL
    };

    action.sa_sigaction = join_handler;
    CHECK(sigaction(SIG_JOIN_REQUEST, &action, NULL), "sigaction join request");
    CHECK(sigaction(SIG_JOIN_HANDLE, &action, NULL), "sigaction join handle");

    action.sa_sigaction = msg_handler;
    CHECK(sigaction(SIG_MSG, &action, NULL), "sigaction msg");

    action.sa_mask = normal_set;
    action.sa_flags = SA_RESTART;
    action.sa_handler = exit_handler;
    CHECK(sigaction(SIGINT, &action, NULL), "sigaction exit");

    return;
}
