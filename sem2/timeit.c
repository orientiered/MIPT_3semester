#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/time.h>

pid_t Fork()
{
    pid_t ret = fork();
    if (ret == -1) {
        perror("Fork error:");
    }

    return ret;
}

const int NOT_LAUNCHED_CODE = 127;

void Execvp(const char *__file, char *const __argv[])
{
    int code = execvp(__file, __argv);
    if (code == -1) {
        perror("Execvp error");
        printf("Failed to execute %s\n", __file);
        exit(NOT_LAUNCHED_CODE);
    }

}

int main(int argc, char *argv[]) {

    if (argc < 2) {
        printf("Expected program to run\n");
        return 0;
    }

    struct timeval start, end;

    pid_t pid = Fork();
    gettimeofday(&start, NULL);

    if (!pid) {
        Execvp(argv[1], &argv[1]);
    } else {
        int wstatus = 0;
        wait(&wstatus);
        if (WEXITSTATUS(wstatus) == NOT_LAUNCHED_CODE) {
            return 1;
        }
        gettimeofday(&end, NULL);
    }

    long int time_usec = end.tv_sec * 1e6 + end.tv_usec -start.tv_sec * 1e6 - start.tv_usec;
    long int minutes = time_usec / (1e6 * 60);
    time_usec -= minutes * (1e6 * 60);

    printf("\n\nreal   %ldm%.3f s\n", minutes, (float) time_usec / 1e6);
    return 0;
}
