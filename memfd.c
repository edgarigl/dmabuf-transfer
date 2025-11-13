#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <linux/udmabuf.h>
#include <linux/memfd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>

#include "memfd.h"

int create_range(struct mapped_range *range, const char *name,
            size_t length, int id)
{
    range->memfd = -1;
    range->addr = MAP_FAILED;
    range->length = length;

    int fd = memfd_create(name, MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0) {
        perror("memfd_create");
        return -1;
    }

    if (ftruncate(fd, (off_t)length) < 0) {
        perror("ftruncate");
        close(fd);
        return -1;
    }

    void *addr =
        mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return -1;
    }

    time_t now = time(NULL);
    snprintf(addr, length, "range[%d] %lu bytes created at %lds\n", id, length, now);
    fputs(addr, stdout);

    if (munmap(addr, length) < 0) {
        perror("munmap");
        close(fd);
        return -1;
    }

    const int seals = F_SEAL_SHRINK | F_SEAL_GROW;
    if (fcntl(fd, F_ADD_SEALS, seals) < 0) {
        perror("fcntl(F_ADD_SEALS)");
        close(fd);
        return -1;
    }

    range->memfd = fd;
    range->addr = MAP_FAILED;
    range->length = length;
    return 0;
}

void destroy_range(struct mapped_range *range)
{
    if (!range)
        return;

    if (range->addr && range->addr != MAP_FAILED)
        munmap(range->addr, range->length);

    if (range->memfd >= 0)
        close(range->memfd);

    range->addr = MAP_FAILED;
    range->memfd = -1;
    range->length = 0;
}
