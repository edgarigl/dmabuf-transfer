#define _LARGEFILE64_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <error.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>

#include "sk.h"

#define UNIX_PREFIX "unix:"
#define UNIXD_PREFIX "unixd:"
#define TCP_PREFIX "tcp:"
#define TCPD_PREFIX "tcpd:"

void send_fd(int conn, int fd) {
    struct msghdr msgh;
    struct iovec iov;
    union {
        struct cmsghdr cmsgh;
        /* Space large enough to hold an 'int' */
        char   control[CMSG_SPACE(sizeof(int))];
    } control_un;

    /* We must transmit at least 1 byte of real data in order
     * to send some other ancillary data. */
    char placeholder = 'A';
    iov.iov_base = &placeholder;
    iov.iov_len = sizeof(char);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    /* Write the fd as ancillary data */
    control_un.cmsgh.cmsg_len = CMSG_LEN(sizeof(int));
    control_un.cmsgh.cmsg_level = SOL_SOCKET;
    control_un.cmsgh.cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(CMSG_FIRSTHDR(&msgh))) = fd;

    int size = sendmsg(conn, &msgh, 0);
    if (size < 0) {
        perror("sendmsg()");
        exit(EXIT_FAILURE);
    }
}

/* Receive file descriptor passed from the server over
 * the already-connected unix domain socket @conn. */
int receive_fd(int conn) {
    struct msghdr msgh;
    struct iovec iov;
    union {
        struct cmsghdr cmsgh;
        /* Space large enough to hold an 'int' */
        char   control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr *cmsgh;

    /* The sender must transmit at least 1 byte of real data
     * in order to send some other ancillary data (the fd). */
    char placeholder;
    iov.iov_base = &placeholder;
    iov.iov_len = sizeof(char);

    msgh.msg_name = NULL;
    msgh.msg_namelen = 0;
    msgh.msg_iov = &iov;
    msgh.msg_iovlen = 1;
    msgh.msg_control = control_un.control;
    msgh.msg_controllen = sizeof(control_un.control);

    int size = recvmsg(conn, &msgh, 0);
    if (size == -1) {
        perror("recvmsg()");
        exit(EXIT_FAILURE);
    }

    assert(size == 1);
    cmsgh = CMSG_FIRSTHDR(&msgh);
    assert(cmsgh);

    assert(cmsgh->cmsg_level == SOL_SOCKET);
    assert(cmsgh->cmsg_type == SCM_RIGHTS);
    return *((int *) CMSG_DATA(cmsgh));
}


int sk_reuseaddr(int fd, bool enable)
{
#ifdef _WIN32
    /* Windows defaults to reuse-addr.  */
    /* http://msdn.microsoft.com/en-us/library/windows/desktop/ms740621.aspx */
#else
    int v = enable;
    int r;


    r = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
            (const char *)&v, sizeof(v));
    if (r) {
        perror("SO_REUSEADDR");
        abort();
    }
    return r;
#endif
}

static int sk_unix_open(const char *descr, bool daemon)
{
    struct sockaddr_un addr;
    int fd, nfd;

    descr += daemon ? strlen(UNIXD_PREFIX) : strlen(UNIX_PREFIX);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    printf("connect to %s\n", descr);

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, descr, sizeof addr.sun_path - 1);

    if (!daemon) {
        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) >= 0)
            return fd;
    }

    unlink(addr.sun_path);
    /* Failed to connect. Bind, listen and accept.  */
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0)
        goto fail;

    listen(fd, 5);
    nfd = accept(fd, NULL, NULL);
    close(fd);
    return nfd;
fail:
    close(fd);
    return -1;
}

static int sk_tcp_open(const char *descr, bool daemon)
{
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd, s;
    char *pos;
    char *host;
    char *port = NULL;
    size_t prefix_len = daemon ? strlen(TCPD_PREFIX) : strlen(TCP_PREFIX);

    pos = (char *) descr + prefix_len;
    while (*pos == '/')
        pos++;

    host = strdup(pos);
    pos = strchr(host, ':');
    if (pos) {
        *pos = 0;
        port = pos + 1;
    }

    /* Now connect to the host and port.  */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    s = getaddrinfo(host, port, &hints, &result);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        exit(EXIT_FAILURE);
    }

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (sfd == -1)
            continue;

        if (daemon) {
            if (bind(sfd, rp->ai_addr, rp->ai_addrlen) == -1) {
                perror("bind");
                exit(EXIT_FAILURE);
            }
            if (listen(sfd, 10) == -1) {
                perror("listen");
                exit(EXIT_FAILURE);
            }
            printf("Waiting on connections to %s:%s\n", host, port);
            sfd = accept(sfd, NULL, NULL);
            if (sfd < 0) {
                perror("listen");
                exit(EXIT_FAILURE);
            }
            break;
        } else {
            if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
                break;
        }
        close(sfd);
    }

    if (rp == NULL) {
        return -1;
    }
    freeaddrinfo(result);

    sk_reuseaddr(sfd, true);
    return sfd;
}

int sk_open(const char *descr)
{
    int fd = -1;

    if (descr == NULL)
        return -1;

    if (strncmp(UNIX_PREFIX, descr, strlen(UNIX_PREFIX)) == 0) {
        /* UNIX.  */
        fd = sk_unix_open(descr, false);
        return fd;
    } else if (strncmp(UNIXD_PREFIX, descr, strlen(UNIXD_PREFIX)) == 0) {
        fd = sk_unix_open(descr, true);
        return fd;
    } else if (strncmp(TCPD_PREFIX, descr, strlen(TCP_PREFIX)) == 0) {
        fd = sk_tcp_open(descr, true);
        return fd;
    } else if (strncmp(TCP_PREFIX, descr, strlen(TCP_PREFIX)) == 0) {
        fd = sk_tcp_open(descr, false);
        return fd;
    }
    return -1;
}
