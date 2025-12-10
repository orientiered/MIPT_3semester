#include "../utils.hpp"

#include <cstring>
#include <stack>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string>
#include <vector>


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

const char *get_real_path(char *prefix, const char *path) {
    static char buf[512] = "";
    if (strlen(prefix) == 0) return path;
    else {
        strcpy(buf, prefix);
        // strcat(buf, "/");
        strcat(buf, path);
        return buf;
    }

    return NULL;
}

struct du_state {
    long logical_sz;
    long physical_sz;
};

ssize_t du(du_state *state, char* prefix, const char *path) {

    struct stat st = {};

    const char *real_path = get_real_path(prefix, path);

    int status = lstat(real_path, &st);
    if (status < 0) {
        perror(real_path);
        return -1;
    }


    state->logical_sz += st.st_size;
    // state->physical_sz +=  (st.st_size + 4096-1) / 4096 * 4; // in k
    state->physical_sz += st.st_blocks / 2;

    // if (get_file_type(st.st_mode) == 'l') {
    //     return 0;
    // }

    if (get_file_type(st.st_mode) != 'd') {
        return st.st_size;
    }

    ssize_t result = 0;
    DIR *dir = opendir(real_path);

    char old_prefix[256] = "";
    strcpy(old_prefix, prefix);

    strcpy(prefix, real_path);
    strcat(prefix, "/");

    for (dirent *entry = readdir(dir); entry != NULL; entry = readdir(dir)) {
        if (strcmp(entry->d_name, ".") == 0) continue;
        if (strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        ssize_t temp = du(state, prefix, entry->d_name);
        if (temp < 0) {
            closedir(dir);
            return temp;
        }

        result += temp;
    }

    strcpy(prefix, old_prefix);

    closedir(dir);

    return result;
}

int main(int argc, const char *argv[]) {
    char prefix_buff[256] = "";

    du_state state = {};
    du(&state, prefix_buff, argv[1]);
    printf("Size of '%s' is %ld bytes and %ld kB \n", argv[1], state.logical_sz, state.physical_sz);
    return 0;
}
