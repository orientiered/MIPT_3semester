#include <stdio.h>
#include <getopt.h>

#include "file_copy.h"

void printHelpMsg() {
    printf("Usage: ./cpcp [-vfih] source1 source2 ... dst\n"
           "\tCopies files source1, source2, ... to dst\n"
           "\tdst may be file (only with one source file) or directory\n"
           "\n"
           "\t-v --verbose     Show log with copied files\n"
           "\t-i --interactive Ask to rewrite file\n"
           "\t-f --force       Rewrite existing files\n"
           "\t By default copy is not performed if dst already exists\n"
           "\t-h --help        Show this message\n"
    );
}

int main(int argc, char *argv[]) {
    struct copy_flags flags = {.only_dir_dst     = false,
                               .rewrite_existing = false,
                               .interactive      = false,
                               .verbose          = false
                              };

    struct option cmd_options[] = {
        {"verbose", no_argument, NULL, 'v' },
        {"force", no_argument, NULL, 'f' },
        {"interactive", no_argument, NULL, 'i' },
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    int ch = 0;
    while ((ch = getopt_long(argc, argv, "vfih", cmd_options, NULL)) != -1) {
        switch(ch) {
            case 'v':
                flags.verbose = true;
                break;
            case 'i':
                flags.interactive = true;
                break;
            case 'f':
                flags.rewrite_existing = true;
                break;
            case 'h':
            case '?':
                printHelpMsg();
                return 0;
                break;
            default:
                ERRPRINTF("getopt returned with code %d\n", ch);
                break;
        }
    }

    if (argc - optind == 2) {
        CpContext_t context = {argv[optind], argv[optind+1], NULL};
        CpErr cp_code = copyFile(&context, &flags);
        if (parseCpErr(&context, cp_code, &flags) == CP_FATAL)
            return CP_FATAL;
    } else {
        flags.only_dir_dst = true;
        for (int idx = optind; idx < argc - 1; idx++) {
            CpContext_t context = {argv[idx], argv[argc-1], NULL};
            CpErr cp_code = copyFile(&context, &flags);
            if (parseCpErr(&context, cp_code, &flags) == CP_FATAL)
                return CP_FATAL;
        }
    }

    return 0;
}
