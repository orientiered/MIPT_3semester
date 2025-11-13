#include "../utils.hpp"
#include <signal.h>

void int_handler(int sign) {
    const char *msg = "Goodbye world\n";
    const size_t len = 15;
    write(0, msg, len);
    // _exit(1);
}

void resize_handler(int sign) {
    //
}

sig_atomic_t exit_flag = false;

int main() {
    CHECK(signal(SIGINT, int_handler), "signal");
    CHECK(signal(SIGWINCH, resize_handler), "signal");
    printf("Hello world\n");

    while (1) {
        pause();
    }

    return 0;
}
