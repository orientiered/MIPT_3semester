#define NO_LOGGING
#include "../utils.hpp"
#include <cstring>
#include <signal.h>
#include <sys/stat.h>

int SIG_0 = SIGUSR1;
int SIG_1 = SIGUSR2;
int SIG_GOT = SIG_0;

sigset_t full_mask;
sigset_t wait_mask;
sigset_t recieve_mask;

sig_atomic_t recieved_bit = -1;

void reciever_handler(int sign) {
    if (sign == SIG_0)
        recieved_bit = 0;
    else if (sign == SIG_1)
        recieved_bit = 1;
}

void sender_handler(int sign) {
    return;
}

int recieve_bit() {
    sigsuspend(&recieve_mask);
    kill(getppid(), SIG_GOT);
    return recieved_bit;
}

int recieve_byte() {
    int byte = 0;
    for (int i = 0; i < 8; i++) {
        byte += recieve_bit() << i;
    }

    return byte;
}


void child [[noreturn]] () {

    struct sigaction action = {
        // .sa_handler = usr12_handler,
        // .sa_mask = full_mask,
        // .sa_flags = 0
        reciever_handler,
        full_mask,
        0,
        NULL
    };

    CHECK(sigaction(SIG_0, &action, NULL), "sigaction");
    CHECK(sigaction(SIG_1, &action, NULL), "sigaction");

    LOG("Child: starting recieving\n");

    int c = 0;
    while ((c = recieve_byte()) != 0) {
        putc(c, stdout);
    }

    putc('\n', stdout);
    fprintf(stderr, "Child: finished recieving\n");
    exit(0);
}

void send_bit(int bit, pid_t pid) {
    if (bit == 0) {
        CHECK(kill(pid, SIG_0), "kill usr1");
    } else {
        CHECK(kill(pid, SIG_1), "kill usr2");
    }

    sigsuspend(&wait_mask);
}

void send_byte(int byte, pid_t pid) {
    for (int i = 0; i < 8; i++) {
        send_bit(byte & 1, pid);
        byte >>= 1;
    }
}

void send_message(pid_t pid, const char *msg, size_t size) {
    for (size_t i = 0; i < size; i++) {
        LOG("Sending byte %c(%d)\n", msg[i], msg[i]);
        send_byte(msg[i], pid);
    }
}

int main(int argc, const char *argv[]) {
    sigemptyset(&full_mask);
    CHECK(sigaddset(&full_mask, SIG_1), "delset");
    CHECK(sigaddset(&full_mask, SIG_0), "delset");
    CHECK(sigaddset(&full_mask, SIG_GOT), "delset");

    wait_mask = full_mask;
    recieve_mask = full_mask;

    CHECK(sigdelset(&wait_mask, SIG_GOT), "delset");

    CHECK(sigdelset(&recieve_mask, SIG_0), "delset");
    CHECK(sigdelset(&recieve_mask, SIG_1), "delset");

    CHECK(sigprocmask(SIG_SETMASK, &full_mask, NULL), "procmask");

    struct sigaction action = {
        // .sa_handler = usr12_handler,
        // .sa_mask = full_mask,
        // .sa_flags = 0
        sender_handler,
        full_mask,
        0,
        NULL
    };

    CHECK(sigaction(SIG_GOT, &action, NULL), "sigaction");

    LOG("Initialization finished\n");

    // spawning child process
    pid_t pid = fork();
    if (pid < 0) {
        perror("Fork");
    } else if (pid == 0) {
        child();
    }

    LOG("Parent: starting send\n");

    if (argc > 1) {
        const char *msg = argv[1];
        size_t msg_len = strlen(msg) + 1;
        send_message(pid, msg, msg_len);
    } else {
        const char *filename = "signal_msg.cpp";
        struct stat st = {};
        stat(filename, &st);
        char *buffer = (char *) calloc(st.st_size + 1, 1);
        FILE * program = fopen(filename, "r");
        size_t bytes_read = fread(buffer, 1, st.st_size, program);
        if (bytes_read != st.st_size) {
            perror("Error when reading file\n");
        }
        buffer[st.st_size] = '\0';

        send_message(pid, buffer, st.st_size+1);

        free(buffer);
        fclose(program);
    }

    fprintf(stderr, "Sender: end\n");
    wait_for_all();

    return 0;
}
