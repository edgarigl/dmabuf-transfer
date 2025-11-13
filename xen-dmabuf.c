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
#include <assert.h>

#include <xengnttab.h>
#include "safeio.h"

struct xen_dmabuf {
    uint32_t num_refs;
    uint32_t refs[];
};

static xengnttab_handle * xen_get_handle(void)
{
    static xengnttab_handle *xgt = NULL;

    if (xgt)
        return xgt;

    xgt = xengnttab_open(NULL, 0);
    if (!xgt) {
        perror("xengnttab_open");
        abort();
    }

    return xgt;
}

static void print_xen_dmabuf(struct xen_dmabuf *xdb)
{
    int i;

    printf("xen-dmabuf blob:\nrefs[%d] = {", xdb->num_refs);
    for (i = 0; i < xdb->num_refs; i++) {
        printf("%x,", xdb->refs[i]);
    }
    printf("};\n\n");
}

static struct xen_dmabuf *xen_dmabuf_serialize(uint32_t vmid, int fd,
        unsigned int num_pages)
{
    xengnttab_handle *xgt = xen_get_handle();
    struct xen_dmabuf *xdb;
    int ret;

    xdb = calloc(1, sizeof *xdb + sizeof xdb->refs[0] * num_pages);
    if (!xdb)
        return NULL;

    ret = xengnttab_dmabuf_imp_to_refs(xgt, vmid, fd, num_pages,
            &xdb->refs[0]);
    if (ret < 0) {
        int saved_errno = errno;
        free(xdb);
        errno = saved_errno;
        return NULL;
    }

    xdb->num_refs = num_pages;
    return xdb;
}

int xen_send_fd(uint32_t vmid, int sk_fd, int dma_fd)
{
    long page_size = sysconf(_SC_PAGESIZE);
    unsigned int num_pages;
    struct xen_dmabuf *xdb;
    struct stat st;
    int ret;
    ssize_t written;

    ret = fstat(dma_fd, &st);
    if (ret)
        return ret;

    num_pages = st.st_size / page_size;

    printf("%s: serialize for vmid=%d\n", __func__, vmid);
    xdb = xen_dmabuf_serialize(vmid, dma_fd, num_pages);
    if (!xdb)
        return -1;

    print_xen_dmabuf(xdb);

    written = safe_write(sk_fd, xdb,
            sizeof *xdb + sizeof xdb->refs[0] * xdb->num_refs);

    free(xdb);
    if (written < 0)
        return -1;

    return (int)written;
}

int xen_receive_fd(uint32_t vmid, int fd)
{
    xengnttab_handle *xgt = xen_get_handle();
    struct xen_dmabuf xdb_hdr;
    struct xen_dmabuf *xdb;
    size_t refs_len;
    uint32_t dma_fd32;
    int dma_fd;
    ssize_t read_ret;
    int ret;

    read_ret = safe_read(fd, &xdb_hdr, sizeof xdb_hdr);
    if (read_ret != (ssize_t)sizeof xdb_hdr)
        return -1;

    refs_len = sizeof(uint32_t) * xdb_hdr.num_refs;
    xdb = calloc(1, sizeof *xdb + refs_len);
    if (!xdb)
        return -1;

    memcpy(xdb, &xdb_hdr, sizeof xdb_hdr);

    read_ret = safe_read(fd, &xdb->refs[0], refs_len);
    if (read_ret != (ssize_t)refs_len) {
        free(xdb);
        return -1;
    }

    printf("%s: de-serialize from vmid=%d\n", __func__, vmid);
    print_xen_dmabuf(xdb);

    ret = xengnttab_dmabuf_exp_from_refs(xgt, vmid, 0, xdb->num_refs,
            xdb->refs, &dma_fd32);
    if (ret < 0) {
        perror("xengnttab_dmabuf_exp_from_refs");
        free(xdb);
        return ret;
    }

    free(xdb);

    dma_fd = dma_fd32;
    return dma_fd;
}
