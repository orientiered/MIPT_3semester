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

void child_function()
{
    printf("\tChild process %d created by %d\n", getpid(), getppid());
    exit(0);
}

int main()
{
    int child_count = 0;
    scanf("%d", &child_count);

    printf("Parent process: %d\n", getpid());

    for (int child = 0; child < child_count; child++) {
        pid_t pid = Fork();

        if (!pid) {
            child_function();
        }
    }

    pid_t closed_pid = 0;
    while ((closed_pid = wait(NULL)) != -1) {
        printf("Terpminated child process %d\n", closed_pid);
    }

    return 0;
}
