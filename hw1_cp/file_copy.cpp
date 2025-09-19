#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <stdio.h>

#include "file_copy.h"

static ssize_t Write(int fd, const void *buf, size_t count);

static const char *findFileName(const char *path);

static FileType getFileType(struct stat *file_info);

static const char *createFileName(const char *src, const char *dst, bool dst_is_dir);

static int getUserChoice(const char *path);

static CpErr copyFileFromFd(int src_fd, int dst_fd);

static ssize_t Write(int fd, const void *buf, size_t count) {
    const uint8_t *byte_buf = (const uint8_t *)buf;

    while (count > 0) {
        ssize_t written = write(fd, byte_buf, count);
        if (written < 0) {
            if (errno == EINTR)
                continue;

            return written;
        }

        count -= written;
        byte_buf += written;
    }

    return count;
}

static const char *findFileName(const char *path) {
    assert(path);
    const char *pos = strrchr(path, '/');
    return pos ? pos+1 : path;
}

static FileType getFileType(struct stat *file_info) {
    assert(file_info);
    switch (file_info->st_mode & S_IFMT) {
        case S_IFDIR:
            return FileType::DIR;
        case S_IFREG:
            return FileType::REGULAR;
        default:
            return FileType::UNSUPPORTED;
    }

}

static const char *createFileName(const char *src, const char *dst, bool dst_is_dir) {
    assert(src);
    assert(dst);

    static char buffer[MAX_PATH_LEN];
    buffer[0] = '\0';

    if (dst_is_dir) {
        const char *src_name = findFileName(src);

        strcat(buffer, dst);
        strcat(buffer, "/"); /*  dir may have or not have /, so adding extra */
        strcat(buffer, src_name);

        return buffer;
    }

    return dst;
}

static int getUserChoice(const char *path) {
    assert(path);
    printf("Rewrite '%s'? (Y/n)\n", path);
    char ans = 0;
    scanf("%c", &ans);
    return ans != 'n';
}

static CpErr copyFileFromFd(int src_fd, int dst_fd) {
    char buffer[BUF_SIZE];
    int bytes_read = 0;
    while ((bytes_read = read(src_fd, buffer, sizeof(buffer))) > 0) {
        int code = Write(dst_fd, buffer, bytes_read);
        if (code < 0) {
            return {CP_ERROR::DST_WRITE, code};
        }
    }

    if (bytes_read < 0) {
        return {CP_ERROR::SRC_READ, bytes_read};
    }

    return {CP_ERROR::SUCCESS, 0};
}

/* =============================== GLOBAL SYMBOLS ================================= */
CpErr copyFile(CpContext_t *context, const struct copy_flags *flags) {
    assert(context); assert(flags);
    const char *src = context->src,
               *dst = context->dst;
    assert(src); assert(dst);

    struct stat src_info = {};
    if (stat(src, &src_info) < 0) {
        return {CP_ERROR::SRC_STAT, errno};
    }

    if (getFileType(&src_info) != FileType::REGULAR) {
        return {CP_ERROR::SRC_NOT_REGULAR, 0};
    }

    struct stat dst_info = {};
    bool dst_is_dir = false;
    bool dst_exists = false;
    if (stat(dst, &dst_info) == 0) {
        switch(getFileType(&dst_info)) {
            case FileType::DIR:
                dst_is_dir = true;
                break;
            case FileType::REGULAR:
                dst_exists = true;
                break;
            default:
                break;
        }
    }

    if (flags->only_dir_dst && !dst_is_dir) {
        return {CP_ERROR::DST_NOT_DIR, 0};
    }

    const char *dst_path = createFileName(src, dst, dst_is_dir);
    context->dst_path = dst_path;

    struct stat real_dst_info = {};
    if (stat(dst_path, &real_dst_info) == 0 && getFileType(&real_dst_info) == FileType::REGULAR) {
        if (!flags->rewrite_existing) {
            if (!flags->interactive) {
                return {CP_ERROR::DST_REWRITE, 0};
            } else if (!getUserChoice(dst_path)) {
                return {CP_ERROR::USR_CANCEL, 0};
            }
        }
    }
    int src_fd = open(src, O_RDONLY);
    if (src_fd < 0) {
        return {CP_ERROR::SRC_OPEN, errno};
    }


    int open_flags = O_WRONLY | O_CREAT | O_TRUNC;

    int dst_fd = open(dst_path, open_flags, src_info.st_mode);
    if (dst_fd < 0) {
        return {CP_ERROR::DST_OPEN, errno};
    }

    struct CpErr copy_status = copyFileFromFd(src_fd, dst_fd);
    if (copy_status.code != CP_ERROR::SUCCESS) return copy_status;

    int dst_close = close(dst_fd);
    if (dst_close < 0) return {CP_ERROR::DST_CLOSE, errno};
    int src_close = close(src_fd);
    if (src_close < 0) return {CP_ERROR::SRC_CLOSE, errno};

    return {CP_ERROR::SUCCESS, 0};
}

int parseCpErr(const CpContext_t *context, const CpErr cp_code, const struct copy_flags *flags) {
    assert(context); assert(flags);
    const char *src = context->src,
               *dst = context->dst;
    assert(src); assert(dst);

    switch (cp_code.code) {
        case CP_ERROR::SUCCESS:
            if (flags->verbose) {
                printf("Copied '%s' to '%s'\n", src, dst);
            }
            break;
        case CP_ERROR::SRC_STAT:
            ERRPRINTF("Can't locate '%s':%s\n", src, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::SRC_NOT_REGULAR:
            ERRPRINTF("'%s' is not regular file\n", src);
            break;
        case CP_ERROR::SRC_OPEN:
            ERRPRINTF("Can't open '%s':%s\n", src, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::DST_NOT_DIR:
            ERRPRINTF("Is not directory:'%s'\n", dst);
            return CP_FATAL;
        case CP_ERROR::DST_REWRITE:
            // if (flags->verbose)
            printf("Already exists: '%s'\n", context->dst_path);
            break;
        case CP_ERROR::DST_OPEN:
            ERRPRINTF("Can't open '%s':%s\n", dst, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::SRC_READ:
            ERRPRINTF("Failed to read '%s':%s\n", src, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::DST_WRITE:
            ERRPRINTF("Failed to write to '%s':%s\n", dst, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::SRC_CLOSE:
            ERRPRINTF("Failed to close '%s':%s\n", src, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::DST_CLOSE:
            ERRPRINTF("Failed to close '%s':%s\n", dst, strerror(cp_code.cp_errno));
            break;
        case CP_ERROR::USR_CANCEL:
            break;
        default:
            assert("Unknown copy error" && false);

    }

    return CP_CONTINUE;
}


