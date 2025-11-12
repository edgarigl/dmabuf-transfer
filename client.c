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

#include "xen-dmabuf.h"
#include "sk.h"

int main(int argc, char *argv[])
{
    long page_size_sys = sysconf(_SC_PAGESIZE);
    const size_t page_size = (size_t)page_size_sys;
    const size_t range_size = page_size * 4;
    const int range_count = 4;
    uint32_t vmid = UINT32_MAX;
    int dma_fd;
    int sk_fd;

    if (argc < 2) {
        printf("USAGE: %s: socket address\n", argv[0]);
        exit(1);
    }

    if (argc > 2) {
        vmid = strtoul(argv[2], NULL, 0);
    }

    /* Create socket.  */
    sk_fd = sk_open(argv[1]);

    if (vmid != UINT32_MAX) {
        dma_fd = xen_receive_fd(vmid, sk_fd);
    } else {
        dma_fd = receive_fd(sk_fd);
    }

    /* Use the buffer */
    {
        size_t len = range_size * range_count;
        unsigned int i;
        void *addr;
        char *buf;
        char c;

        safe_read(dma_fd, &c, 1);
        printf("c=%d\n", c);

        addr = mmap(0, len, PROT_READ|PROT_WRITE, MAP_SHARED, dma_fd, 0);
        printf("mmap %p\n", addr);

        if (addr != MAP_FAILED) {
            buf = addr;
            for (i = 0; i < len; i += 1024) {
                printf("%x ", buf[i]);
            }
            printf("\n");
            munmap(addr, len);
        }
    }

    close(sk_fd);
	close(dma_fd);
	return EXIT_SUCCESS;
}
