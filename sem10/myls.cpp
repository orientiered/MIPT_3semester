#include "../utils.hpp"

#include <cstring>
#include <stack>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include <vector>

#include <pwd.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>

#include "argvProcessor.h"

struct ls_ctx {
    bool recursive;
    bool dir;
    bool long_out;
    bool print_dirname;
    bool all;
    bool inode;
    bool numeric;
};

bool is_hidden(const char *name) {
    const char *slash = strrchr(name, '/');
    if (slash) {
        return slash[1] == '.';
    }
    return name[0] == '.';
}

char get_file_type(int type) {
    switch(type & S_IFMT) {
        case S_IFSOCK:
            return 's';
        case S_IFLNK:
            return 'l';
        case S_IFREG:
            return '-';
        case S_IFDIR:
            return 'd';
        case S_IFIFO:
            return 'f';
        default:
            return '?';
    }
}

int ls(ls_ctx *ctx, const std::vector<const char *>& paths);

int ls_base(ls_ctx *ctx, const std::string& path, std::stack<std::string>& dirs);

int ls_print(ls_ctx *ctx, struct stat& st, const char *name, const char *path);

int ls_base(ls_ctx *ctx, const std::string& path, std::stack<std::string>& dirs) {
    struct stat st = {};
    // LOG("Statting %s\n", path.c_str());
    int status = lstat(path.c_str(), &st);

    if (status < 0) {
        perror(path.c_str());
        return 1;
    }

    if (get_file_type(st.st_mode) != 'd') {
        return ls_print(ctx, st, path.c_str(), path.c_str());
    }

    DIR *dir = opendir(path.c_str());

    if (ctx->print_dirname) {
        printf("%s:\n", path.c_str());
    }

    for (dirent* entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
        const char *name = entry->d_name;
        const std::string new_path = path + "/" + name;
        lstat(new_path.c_str(), &st);

        if (!is_hidden(name) || ctx->all) {
            if (ctx->recursive && get_file_type(st.st_mode) == 'd' && strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                dirs.push(new_path);
                // LOG("Pushed path: %s\n", new_path.c_str());
            }
            ls_print(ctx, st, name, new_path.c_str());
        }
    }

    SOFT_CHECK(closedir(dir), "closedir");

    return 0;
}

int ls(ls_ctx *ctx, const std::vector<const char *>& paths) {
    std::stack<std::string> dirs;
    for (int i = paths.size() - 1; i >= 0; i--) {
        dirs.push(std::string(paths[i]));
    }

    bool first_dir = true;

    while (!dirs.empty()) {
        const std::string path = std::move(dirs.top());
        dirs.pop();

        if (!first_dir)
            printf("\n");

        ls_base(ctx, path, dirs);
        first_dir = false;
    }

    return 0;
}


int ls_print(ls_ctx *ctx, struct stat& st, const char *name, const char *path) {
    if (ctx->inode) {
        printf("%6lu ", st.st_ino);
    }

    char file_type = get_file_type(st.st_mode);
    if (ctx->long_out) {
        printf("%c %o %lu ", file_type, st.st_mode & 0777, st.st_nlink);

        // %8ld
        int uid = st.st_uid;
        struct passwd* uid_passwd = getpwuid(uid);
        int gid = st.st_gid;
        struct passwd* gid_passwd = getpwuid(gid);

        if (ctx->numeric) {
            printf("%d %d ", uid, gid);
        } else {
            printf("%s %s ", uid_passwd->pw_name, gid_passwd->pw_name);
        }

        char datestring[256];
        struct tm *tm = localtime(&st.st_mtime);
        strftime(datestring, 256, nl_langinfo(D_T_FMT), tm);

        printf("%8ld %s ", st.st_size, datestring);
        // if (
    } else {
        printf("\t");
    }

    const char *start_seq = "";
    const char *end_seq = COL_RESET;
    if (st.st_mode & 0100) start_seq = COL_BOLD COL_GREEN;
    if (file_type == 'd') start_seq = COL_BOLD COL_BLUE;
    if (file_type == 'l') start_seq = COL_BOLD COL_CYAN;

    printf("%s%s%s", start_seq, name, end_seq);
    if (file_type == 'l') {
        char buffer[1024];
        int link_bytes = readlink(path, buffer, 1024);
        buffer[link_bytes] = '\0';

        printf(" -> %s", buffer);
    }

    printf("\n");
    return 0;
}


int main(int argc, const char *argv[]) {

    registerFlag(TYPE_BLANK, "-a", "--all", "Show hidden files");
    registerFlag(TYPE_BLANK, "-l", "--long", "Show more info about file");
    registerFlag(TYPE_BLANK, "-R", "--recursive", "Show files recursively");
    registerFlag(TYPE_BLANK, "-i", "--inode", "Show index node");
    registerFlag(TYPE_BLANK, "-n", "--numeric", "Show user id and group id instead of name");
    registerFlag(TYPE_BLANK, "-d", "--directory", "List directories themselves, not their contents");

    enableHelpFlag("Usage: ./myls.exe [flags] [path1 path2 ...]");

    processArgs(argc, argv);

    ls_ctx ctx = {};
    ctx.all         = isFlagSet("-a");
    ctx.long_out    = isFlagSet("-l");
    ctx.recursive   = isFlagSet("-R");
    ctx.dir         = isFlagSet("-d");
    ctx.inode       = isFlagSet("-i");
    ctx.numeric     = isFlagSet("-n");

    int total_paths = 0;
    while (getDefaultArgument(total_paths) != NULL) total_paths++;

    std::vector<const char *> paths;
    if (total_paths == 0) {
        paths = {"."};
    } else {
        for (int i = 0; i < total_paths; i++) {
            paths.push_back(getDefaultArgument(i));
        }
    }

    ctx.print_dirname = (ctx.recursive || paths.size() > 1);

    ls(&ctx, paths);

    return 0;
}
