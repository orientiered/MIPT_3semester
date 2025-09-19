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

int main()
{
    int child_count = 0;
    scanf("%d", &child_count);

    printf("Parent process: %d\n", getpid());

    for (int child = 0; child < child_count; child++) {
        pid_t pid = Fork();

        if (!pid) {
            printf("\tChild process %d created by %d\n", getpid(), getppid());
        } else {
            // parent stops
            break;
        }
    }

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {
        printf("Terminated child process %d by %d\n", closed_pid, getpid());
    }

    return 0;
}
