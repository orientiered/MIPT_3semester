#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>

int main() {
    printf("Pid: %d\n"
	   "Parent pid: %d\n",
	   getpid(), getppid());
    
    return 0;
}
