#ifndef FILE_COPY_H
#define FILE_COPY_H
#include <stdlib.h>

const int MAX_PATH_LEN = 512;
const size_t BUF_SIZE = 4096;

enum class FileType {
    UNSUPPORTED,
    DIR,
    REGULAR,
};

struct copy_flags {
    bool only_dir_dst;
    bool rewrite_existing;
    bool interactive;
    bool verbose;
};

enum class CP_ERROR {
    SUCCESS = 0,
    SRC_STAT,
    SRC_NOT_REGULAR,
    SRC_OPEN,
    DST_NOT_DIR,
    DST_REWRITE,
    DST_OPEN,
    SRC_READ,
    DST_WRITE,
    SRC_CLOSE,
    DST_CLOSE,
    USR_CANCEL,
};

struct CpErr {
    CP_ERROR code;
    int cp_errno;
};


typedef struct CpContext {
    const char *src;
    const char *dst;
    const char *dst_path; ///< Out parameter
} CpContext_t;

#define ERRPRINTF(...) fprintf(stderr, __VA_ARGS__)

CpErr copyFile(CpContext_t *context, const struct copy_flags *flags);

const int CP_CONTINUE = 4;
const int CP_FATAL = 5;
int parseCpErr(const CpContext_t *context, const CpErr cp_code, const struct copy_flags *flags);


#endif
