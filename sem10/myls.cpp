#include "../utils.hpp"

#include <cstring>
#include <stack>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include <vector>

#include "argvProcessor.h"

const size_t MAX_PREFIX = 256;

struct ls_ctx {
    int depth = 0;
    bool recursive;
    bool dir;
    bool long_out;
    bool print_dirname;
    bool all;
    std::string path_prefix; // must end with / or be empty
    // char path_prefix[MAX_PREFIX];
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

int ls_print(ls_ctx *ctx, struct stat& st, const char *name);

int ls_base(ls_ctx *ctx, const std::string& path, std::stack<std::string>& dirs) {
    struct stat st = {};
    // LOG("Statting %s\n", path.c_str());
    int status = lstat(path.c_str(), &st);

    if (status < 0) {
        perror(path.c_str());
        return 1;
    }

    if (get_file_type(st.st_mode) != 'd') {
        return ls_print(ctx, st, path.c_str());
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
            ls_print(ctx, st, name);
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

    while (!dirs.empty()) {
        const std::string path = dirs.top();
        dirs.pop();
        printf("\n");
        ls_base(ctx, path, dirs);

    }

    return 0;
}


int ls_print(ls_ctx *ctx, struct stat& st, const char *name) {
    if (ctx->long_out) {
        printf("%c %o %ld %s\n", get_file_type(st.st_mode), st.st_mode & 0777, st.st_size, name);
    } else {
        printf("\t%s\n", name);
    }

    return 0;
}


int main(int argc, const char *argv[]) {

    registerFlag(TYPE_BLANK, "-a", "--all", "Show hidden files");
    registerFlag(TYPE_BLANK, "-l", "--long", "Show more info about file");
    registerFlag(TYPE_BLANK, "-R", "--recursive", "Show files recursively");

    enableHelpFlag("Usage: ./myls.exe [flags] [path1 path2 ...]");

    processArgs(argc, argv);

    ls_ctx ctx = {};
    if (isFlagSet("-a")) ctx.all = true;
    if (isFlagSet("-l")) ctx.long_out = true;
    if (isFlagSet("-R")) ctx.recursive = true;

    int total_paths = 0;
    while (getDefaultArgument(total_paths) != NULL) total_paths++;

    ctx.print_dirname = true;
    std::vector<const char *> paths;
    if (total_paths == 0) {
        paths = {"."};
    } else if (total_paths == 1) {
        paths = {getDefaultArgument(0)};
    } else {
        for (int i = 0; i < total_paths; i++) {
            paths.push_back(getDefaultArgument(i));
        }
    }

    ls(&ctx, paths);

    return 0;
}
