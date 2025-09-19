#include <stdio.h>
#include <string.h>
#include <stdbool.h>

int main(const int argc, const char *argv[]) 
{
	bool trailing_newline = true;
	unsigned start_arg_idx = 1; // first argument is name of program

	if (argc > 1 && strcmp("-n", argv[1]) == 0 ) {
		trailing_newline = false;
		start_arg_idx++;
	}

	for (unsigned arg_idx = start_arg_idx; arg_idx < argc; arg_idx++) {
		printf("%s", argv[arg_idx]);
		if (arg_idx + 1 < argc)
			printf(" ");
	}
	
	if (trailing_newline) {
		printf("\n");
	}
	return 0;
}
