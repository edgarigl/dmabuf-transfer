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

#include "sk.h"
#include "memfd.h"
#include "xen-dmabuf.h"

int create_udmabuf(struct mapped_range *ranges, const int range_count,
        const size_t range_size, const size_t page_size)
{
    struct udmabuf_create_list *create;
    size_t create_size;
    int udmabuf;
    int dma_fd;
    int i;

    for (i = 0; i < range_count; i++) {
        int j;

        if (create_range(&ranges[i], "udmabuf-range", range_size, i + 1) < 0) {
            fprintf(stderr, "Failed to create range %d, aborting.\n", i);
            for (j = 0; j <= i; j++)
                destroy_range(&ranges[j]);
            return EXIT_FAILURE;
        }
    }

    udmabuf = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
    if (udmabuf < 0) {
        perror("open /dev/udmabuf");
        for (i = 0; i < range_count; i++)
            destroy_range(&ranges[i]);
        return EXIT_FAILURE;
    }

    create_size = sizeof(struct udmabuf_create_list) +
        (size_t)range_count *
        sizeof(struct udmabuf_create_item);
    create = calloc(1, create_size);
    if (!create) {
        fprintf(stderr, "Failed to allocate udmabuf_create_list\n");
        close(udmabuf);
        for (i = 0; i < range_count; i++)
            destroy_range(&ranges[i]);
        return EXIT_FAILURE;
    }

    create->flags = UDMABUF_FLAGS_CLOEXEC;
    create->count = range_count;
    for (i = 0; i < range_count; i++) {
        create->list[i].memfd = ranges[i].memfd;
        create->list[i].offset = 0;
        create->list[i].size = ranges[i].length;
    }

    dma_fd = ioctl(udmabuf, UDMABUF_CREATE_LIST, create);
    if (dma_fd < 0) {
        perror("ioctl UDMABUF_CREATE_LIST");
        free(create);
        close(udmabuf);
        for (i = 0; i < range_count; i++)
            destroy_range(&ranges[i]);
        return EXIT_FAILURE;
    }

    printf("Created dma-buf FD %d spanning %d ranges (%zu bytes each).\n",
            dma_fd, range_count, range_size);

    free(create);
    close(udmabuf);
    return dma_fd;
}

int main(int argc, char *argv[])
{
    const size_t page_size = sysconf(_SC_PAGESIZE);
    const size_t range_size = page_size * 4;
    const int range_count = 4;
    struct mapped_range ranges[range_count];
    uint32_t vmid = UINT32_MAX;
    int dma_fd;
    int sk_fd;
    int i;

    if (argc < 2) {
        printf("USAGE: %s: socket address\n", argv[0]);
        exit(1);
    }

    if (argc > 2) {
        vmid = strtoul(argv[2], NULL, 0);
    }

    /* Create user-space DMABUF.  */
    memset(ranges, 0, sizeof(ranges));
    dma_fd = create_udmabuf(ranges, range_count, range_size, page_size);

    /* Create socket.  */
    sk_fd = sk_open(argv[1]);

    /* Transfer DMABUF to peer.  */
    if (vmid != UINT32_MAX) {
        xen_send_fd(vmid, sk_fd, dma_fd);
    } else {
        unix_send_fd(sk_fd, dma_fd);
    }

    /* Since we don't have any handshake, wait for a while before destroying. */
    sleep(2);

    for (i = 0; i < range_count; i++)
        destroy_range(&ranges[i]);

    close(sk_fd);
    close(dma_fd);
    return EXIT_SUCCESS;
}
