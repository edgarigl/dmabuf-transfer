#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
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

#include "xen-dmabuf.h"
#include "safeio.h"
#include "sk.h"

int main(int argc, char *argv[])
{
    const size_t page_size = sysconf(_SC_PAGESIZE);
    uint32_t vmid = UINT32_MAX;
    struct stat st;
    int dma_fd;
    int sk_fd;
    int ret;
    int i;

    if (argc < 2) {
        printf("USAGE: %s: socket address\n", argv[0]);
        exit(1);
    }

    if (argc > 2) {
        vmid = strtoul(argv[2], NULL, 0);
    }

    /* Create socket.  */
    sk_fd = sk_open(argv[1]);
    if (sk_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    if (vmid != UINT32_MAX) {
        dma_fd = xen_receive_fd(vmid, sk_fd);
    } else {
        dma_fd = unix_receive_fd(sk_fd);
    }

    ret = fstat(dma_fd, &st);
    if (ret) {
        perror("fstat");
        goto err;
    }

    /* Use the buffer */
    if (st.st_size)
    {
        void *addr;

        addr = mmap(0, st.st_size, PROT_READ, MAP_SHARED, dma_fd, 0);

        if (addr != MAP_FAILED) {
            for (i = 0; i < st.st_size; i += 4 * page_size) {
                fputs(addr + i, stdout);
            }
            munmap(addr, st.st_size);
        } else {
            perror("mmap");
        }
    }

err:
    close(sk_fd);
    close(dma_fd);
    return EXIT_SUCCESS;
}
