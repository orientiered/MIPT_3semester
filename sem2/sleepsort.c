#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>

pid_t Fork()
{
    pid_t ret = fork();
    if (ret == -1) {
        perror("Fork error:");
    }

    return ret;
}

void child_sort(int num, int shift)
{
    // printf("\tChild process %d created by %d\n", getpid(), getppid());
    usleep((num - shift)*1000);
    printf("%d ", num);
    exit(0);
}


int main(const int argc, const char *argv[])
{
    int shift = 1e9;
    for (int i = 1; i < argc; i++) {
        int num = atoi(argv[i]);
        shift = (num < shift) ? num : shift;
    }

    for (int child = 0; child < argc-1; child++) {
        pid_t pid = Fork();

        if (!pid) {
            int my_number = atoi(argv[child+1]);
            child_sort(my_number, shift);
        }
    }

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {
        // printf("Terminated child process %d\n", closed_pid);
    }

    printf("\nSorted\n");

    return 0;
}
