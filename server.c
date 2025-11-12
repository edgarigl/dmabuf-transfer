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
#include "xen-dmabuf.h"

struct mapped_range {
	int memfd;
	void *addr;
	size_t length;
};

static int create_range(struct mapped_range *range, const char *name,
			size_t length, uint8_t fill)
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

	memset(addr, fill, length);

	if (munmap(addr, length) < 0) {
		perror("munmap");
		close(fd);
		return -1;
	}

	const int seals = F_SEAL_SHRINK | F_SEAL_GROW; // | F_SEAL_WRITE;
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

static void destroy_range(struct mapped_range *range)
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

int main(int argc, char *argv[])
{
	long page_size_sys = sysconf(_SC_PAGESIZE);
	const size_t page_size = (size_t)page_size_sys;
	const size_t range_size = page_size * 4;
	const int range_count = 4;
	struct mapped_range ranges[range_count];
    uint32_t vmid = UINT32_MAX;
    int sk_fd;

    if (argc < 2) {
        printf("USAGE: %s: socket address\n", argv[0]);
        exit(1);
    }

    if (argc > 2) {
        vmid = strtoul(argv[2], NULL, 0);
    }

	if (page_size_sys <= 0) {
		fprintf(stderr, "Failed to query page size\n");
		return EXIT_FAILURE;
	}

	memset(ranges, 0, sizeof(ranges));
	for (int i = 0; i < range_count; ++i) {
		if (create_range(&ranges[i], "udmabuf-range", range_size, i + 1) < 0) {
			fprintf(stderr, "Failed to create range %d, aborting.\n", i);
			for (int j = 0; j <= i; ++j)
				destroy_range(&ranges[j]);
			return EXIT_FAILURE;
		}
	}

	int udmabuf = open("/dev/udmabuf", O_RDWR | O_CLOEXEC);
	if (udmabuf < 0) {
		perror("open /dev/udmabuf");
		for (int i = 0; i < range_count; ++i)
			destroy_range(&ranges[i]);
		return EXIT_FAILURE;
	}

	size_t create_size = sizeof(struct udmabuf_create_list) +
			     (size_t)range_count *
				     sizeof(struct udmabuf_create_item);
	struct udmabuf_create_list *create = calloc(1, create_size);
	if (!create) {
		fprintf(stderr, "Failed to allocate udmabuf_create_list\n");
		close(udmabuf);
		for (int i = 0; i < range_count; ++i)
			destroy_range(&ranges[i]);
		return EXIT_FAILURE;
	}

	create->flags = UDMABUF_FLAGS_CLOEXEC;
	create->count = range_count;
	for (int i = 0; i < range_count; ++i) {
		create->list[i].memfd = (uint32_t)ranges[i].memfd;
		create->list[i].offset = 0;
		create->list[i].size = ranges[i].length;
	}

	int dma_fd = ioctl(udmabuf, UDMABUF_CREATE_LIST, create);
	if (dma_fd < 0) {
		perror("ioctl UDMABUF_CREATE_LIST");
		free(create);
		close(udmabuf);
		for (int i = 0; i < range_count; ++i)
			destroy_range(&ranges[i]);
		return EXIT_FAILURE;
	}

	printf("Created dma-buf FD %d spanning %d ranges (%zu bytes each).\n",
	       dma_fd, range_count, range_size);

    /* Create socket.  */
    sk_fd = sk_open(argv[1]);

    if (vmid != UINT32_MAX) {
        xen_send_fd(vmid, sk_fd, dma_fd);
        sleep(1000);
    } else {
        send_fd(sk_fd, dma_fd);
    }

    close(sk_fd);

	free(create);
	close(dma_fd);
	close(udmabuf);
	for (int i = 0; i < range_count; ++i)
		destroy_range(&ranges[i]);

	return EXIT_SUCCESS;
}
