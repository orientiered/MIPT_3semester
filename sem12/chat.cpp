#include "../utils.hpp"
#include <cstring>
#include <signal.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

const size_t MAX_USERS = 256;

const size_t MAX_MSG_SIZE = 256;
#define MSG_FMT " %255[^\n]" // must be MAX_MSG_SIZE-1
#define CMD_FMT "%255s " // read string and spaces after it
const size_t QUEUE_BLOCK_SIZE = MAX_MSG_SIZE + 20;
const size_t MSG_QUEUE_LEN = 32;
const size_t MSG_QUEUE_SIZE = QUEUE_BLOCK_SIZE * MSG_QUEUE_LEN;

struct chat_info {
    pid_t users[MAX_USERS];
    size_t user_cnt = 0;

    char msg_queue[MSG_QUEUE_SIZE];
    int  msg_idx = 0;

    int mutex_id = -1; // sys v semaphore
};

/* ========== RT SIGNAL constants and handler prototypes ========= */

const int SIG_JOIN_REQUEST = SIGRTMIN;
const int SIG_JOIN_HANDLE  = SIGRTMIN + 1;

const int SIG_MSG = SIGRTMIN + 2;

void join_handler(int signum, siginfo_t *siginfo, void *);

void exit_handler(int signum);

void msg_handler(int signum, siginfo_t *siginfo, void *);


/* ================= Chat class =============================== */
enum ChatStatus {
    BAD_PID    =  2,    // pid not registered in users
    BAD_FORMAT =  1,    // wrong format for /tell command
    SUCCESS    =  0,
    CHAT_ERR   = -1,    // General error
    SCANF_FAIL = -2,    // EOF or other scanf error
    QUIT_CMD   = -3,    // /bye command
    USER_OVERFLOW = -4  // User limit exceeded
};

struct chat {
    chat_info *info = nullptr;
    int my_id = -1;
    int shm_fd = -1;
    int mtx_id = -1;

    char msg_buf[256] = {};
    static const size_t msg_buf_size = 255;

    char *line = nullptr;
    char *line_ptr = nullptr;
    size_t line_size = 0;

    /* ================== Mutex lock and unlock ==================== */

    int mutex_lock() {
        sembuf sop = {0, -1, 0};
        //TODO: timed wait to fix mutex if process was killed with catched mutex
        return semop(mtx_id, &sop, 1);
    }

    int mutex_unlock() {
        sembuf sop = {0, +1, 0};
        return semop(mtx_id, &sop, 1);
    }

    /* ================== Add and remove from users table ========== */

    /// @brief Add current process to the users table
    int add_to_users() {
        mutex_lock();

        if (info->user_cnt >= MAX_USERS) {
            mutex_unlock();
            return USER_OVERFLOW;
        }

        for (int i = 0; i < MAX_USERS; i++) {
            if (info->users[i] == 0) {
                info->users[i] = getpid();
                my_id = i;
                info->user_cnt++;
                PROC_LOG("Current user count %zu\n", info->user_cnt);
                mutex_unlock();
                return SUCCESS;
            }
        }

        mutex_unlock();
        return CHAT_ERR;
    }

    /// @brief Remove current process from the users table
    //! not async-safe
    int leave_from_users() {

        if (my_id < 0) return SUCCESS;


        info->users[my_id] = 0;
        info->user_cnt--;

        my_id = -1;
        PROC_LOG("Leave: current user count %zu\n", info->user_cnt);

        return SUCCESS;
    }


    /// @brief Remove process with given pid from users table
    //! not async-safe
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

    /* ========================== Chat init function ======================= */

    /// @brief Create new chat with join_pid < 0 or join to chat with process join_pid
    int join(pid_t join_pid) {
        if (join_pid < 0) {
            PROC_LOG("Creating shmem\n");

            shm_fd = shmget(IPC_PRIVATE, sizeof(chat_info), IPC_CREAT | 0666);
            CHECK(shm_fd, "Create shm");
            info = (chat_info *) shmat(shm_fd, NULL, 0);
            CHECK(info, "Attach shm");

            info->mutex_id = semget(IPC_PRIVATE, 1, IPC_CREAT | 0666);
            sembuf sops = {.sem_num = 0, .sem_op = 1, .sem_flg = 0};
            CHECK(info->mutex_id, "mutex create");
            CHECK(semop(info->mutex_id, &sops, 1), "mutex init");
            mtx_id = info->mutex_id;

            add_to_users();

            PROC_LOG("Basic init finished\n");

            return 0;
        }

        PROC_LOG("Sending join request\n");
        // TODO: check siqgueue
        sigqueue(join_pid, SIG_JOIN_REQUEST, {0});

        siginfo_t join_info = {};
        sigset_t set = {};
        CHECK(sigaddset(&set, SIG_JOIN_HANDLE), "addset");

        PROC_LOG("Waiting for join answer\n");
        CHECK(sigwaitinfo(&set, &join_info), "join wait handle");

        shm_fd = join_info.si_value.sival_int;
        info = (chat_info *) shmat(shm_fd, NULL, 0);
        CHECK(info, "Attach shm");
        mtx_id = info->mutex_id;

        add_to_users();

        PROC_LOG("Attached to chat with process %d\n", join_info.si_pid);

        return 0;
    }

    //! not async-safe
    /// @brief Send SIG_MSG with value msg_idx (index in msg queue) to process pid
    /// @brief optionally check whether user with given pid exists (check_pid)s
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

        int status = sigqueue(pid, SIG_MSG, {msg_idx});

        // Deleting dead users
        if (status < 0 && errno == ESRCH) {
            delete_from_users(pid);
        } else SOFT_CHECK(status, "sigqueue");

        return 0;
    }

    //! not async-safe
    /// @brief Copy string from buf to msg_queue and return it's index
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
            fprintf(stderr, "Usage: /tell <pid> msg\n");
            return BAD_FORMAT;
        }
        line_ptr+=symbols;

        char buf[QUEUE_BLOCK_SIZE] = {};
        snprintf(buf, sizeof(buf), "[%d told you]: %s\n", getpid(), line_ptr);

        mutex_lock();
        int msg_idx = write_msg(buf);
        int status = send_msg(pid, msg_idx, true);
        mutex_unlock();

        return status;
    }

    int handle_say() {
        char buf[QUEUE_BLOCK_SIZE] = {};
        snprintf(buf, sizeof(buf), "%d: %s\n", getpid(), line_ptr);

        mutex_lock();
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

        mutex_unlock();
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
            } else if (strcmp(msg_buf, "/me") == 0) {
                printf("[Me: pid %d, id %d]\n", getpid(), my_id);
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

        mutex_lock();
        // last user closes shmem and mutex

        int users_left = info->user_cnt;
        leave_from_users();
        CHECK(shmdt(info), "shm detach");
        info = nullptr;

        mutex_unlock();
        if (users_left == 1) {
            CHECK(shmctl(shm_fd, IPC_RMID, NULL), "shm unlink");
            CHECK(semctl(mtx_id, 1, IPC_RMID), "mutex unlink");
        }

        shm_fd = -1;
        mtx_id = -1;

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

    if (chat.join(join_pid) < 0) {
        fprintf(stderr, "Failed to create/connect to chat\n");
        return EXIT_FAILURE;
    }

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
