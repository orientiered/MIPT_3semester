#pragma once
#include "utils.hpp"

#include <sys/mman.h>
#include <fcntl.h>

struct shmem_manager {
    const char *name = nullptr;
    size_t capacity = 0;
    size_t size = 0;

    int fd = -1;
    char *sh_mem = nullptr;

    shmem_manager(const char *shm_name, size_t start_cap): name(shm_name) {
        fd = shm_open(shm_name, O_RDWR | O_CREAT, 0666);
        CHECK(fd, "shm_open");
        ftruncate(fd, start_cap);
        capacity = start_cap;

        sh_mem = (char *) mmap(NULL, capacity, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        CHECK(sh_mem, "mmap");
    }

    void *get_shared_mem(size_t nbytes) {
        if (size + nbytes > capacity) {
            LOG("Not enough shared memory to allocate shmem\n");
            return nullptr;
        }

        void *result = sh_mem + size;
        size += nbytes;
        return result;

    }

    template <typename T>
    T* get_shared() {
        return (T*) get_shared_mem(sizeof(T));
    }

    template <typename T>
    T* get_shared(size_t n) {
        return (T*) get_shared_mem(sizeof(T)*n);
    }


    ~shmem_manager() {
        SOFT_CHECK(munmap(sh_mem, capacity), "unmap");
        SOFT_CHECK(shm_unlink(name), "unlink");
    }
};
