/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <zephyr/net/socket.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/net/socket_offload.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_context.h>
#include <zephyr/net/dns_resolve.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/fdtable.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/fs/fs.h>
#include <errno.h>
#include <stdlib.h>

/* Use sockets_internal.h from Zephyr's net subsystem */
#include "sockets_internal.h"
#include "qc7xx_socket_offload.h"
#include "qc7xx_ipc_socket.h"

#if defined(CONFIG_NET_SOCKETS_SOCKOPT_TLS)
#include "tls_internal.h"
#endif


/* Increment by 1 to make sure we do not store the value of 0, which has
 * a special meaning in the fdtable subsys.
 */
#define SD_TO_OBJ(sd) ((void *)(sd + 1))
#define OBJ_TO_SD(obj) (((int)(intptr_t)(obj)) - 1)

/* Forward declarations */
static int qc7xx_socket_close(void *obj);
static ssize_t qc7xx_socket_read(void *obj, void *buf, size_t len);
static ssize_t qc7xx_socket_write(void *obj, const void *buf, size_t len);
static ssize_t qc7xx_socket_sendto(void *obj, const void *buf, size_t len, int flags,
                                   const struct sockaddr *dest_addr, socklen_t addrlen);
static ssize_t qc7xx_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
                                     struct sockaddr *src_addr, socklen_t *addrlen);
static int qc7xx_socket_bind(void *obj, const struct sockaddr *addr, socklen_t addrlen);
static int qc7xx_socket_connect(void *obj, const struct sockaddr *addr, socklen_t addrlen);
static int qc7xx_socket_listen(void *obj, int backlog);
static int qc7xx_socket_accept(void *obj, struct sockaddr *addr, socklen_t *addrlen);
static int qc7xx_socket_getsockopt(void *obj, int level, int optname,
                                   void *optval, socklen_t *optlen);
static int qc7xx_socket_setsockopt(void *obj, int level, int optname,
                                   const void *optval, socklen_t optlen);
static int qc7xx_socket_getsockname(void *obj, struct sockaddr *addr, socklen_t *addrlen);
static int qc7xx_socket_getpeername(void *obj, struct sockaddr *addr, socklen_t *addrlen);
static int qc7xx_socket_shutdown(void *obj, int how);

/* Socket operation virtual function table */
static const struct socket_op_vtable qc7xx_socket_fd_op_vtable = {
    .fd_vtable = {
        .read = qc7xx_socket_read,
        .write = qc7xx_socket_write,
        .close = qc7xx_socket_close,
    },
    .shutdown = qc7xx_socket_shutdown,
    .bind = qc7xx_socket_bind,
    .connect = qc7xx_socket_connect,
    .listen = qc7xx_socket_listen,
    .accept = qc7xx_socket_accept,
    .sendto = qc7xx_socket_sendto,
    .recvfrom = qc7xx_socket_recvfrom,
    .getsockopt = qc7xx_socket_getsockopt,
    .setsockopt = qc7xx_socket_setsockopt,
    .getsockname = qc7xx_socket_getsockname,
    .getpeername = qc7xx_socket_getpeername,
};

/* DNS Offload implementation */
static int qc7xx_offload_getaddrinfo(const char *node, const char *service,
                                     const struct zsock_addrinfo *hints,
                                     struct zsock_addrinfo **res)
{
    ARG_UNUSED(hints);
    ARG_UNUSED(res);

    int ret;
    char port_str[6];

    if (!node && !service) {
        return DNS_EAI_NONAME;
    }

    /* Convert service to port string */
    if (service) {
        ret = snprintf(port_str, sizeof(port_str), "%s", service);
        if (ret < 0 || ret >= sizeof(port_str)) {
            return DNS_EAI_SERVICE;
        }
    }

    /* TODO: Implement DNS query via IPC to NP core */
    /* For now, return a basic response that indicates not implemented */
    return DNS_EAI_FAIL;
}

static void qc7xx_offload_freeaddrinfo(struct zsock_addrinfo *res)
{
    struct zsock_addrinfo *ai, *ai_next;

    for (ai = res; ai != NULL; ai = ai_next) {
        ai_next = ai->ai_next;
        if (ai->ai_addr != NULL) {
            k_free(ai->ai_addr);
        }
        if (ai->ai_canonname != NULL) {
            k_free(ai->ai_canonname);
        }
        k_free(ai);
    }
}

static const struct socket_dns_offload qc7xx_dns_offload = {
    .getaddrinfo = qc7xx_offload_getaddrinfo,
    .freeaddrinfo = qc7xx_offload_freeaddrinfo,
};

bool qc7xx_socket_is_supported(int family, int type, int proto)
{
    /* Check if the socket family is supported */
    if (family != AF_INET && family != AF_INET6 && family != AF_UNSPEC) {
        return false;
    }

    /* Check if the socket type is supported */
    if (type != SOCK_STREAM && type != SOCK_DGRAM) {
        return false;
    }

    /* Check if the protocol is supported */
    if (proto != IPPROTO_TCP && proto != IPPROTO_UDP && proto != 0) {
        return false;
    }

    /* Additional checks for unsupported combinations */
    if (family == AF_INET6 && type == SOCK_DGRAM && proto == IPPROTO_TCP) {
        return false;
    }
    if (family == AF_INET && type == SOCK_STREAM && proto == IPPROTO_UDP) {
        return false;
    }

    return true;
}

int qc7xx_socket_create(int family, int type, int proto)
{
    int fd;
    int np_sockfd;
    void *sock_obj;

    
    /* Validate socket parameters */
    if (!qc7xx_socket_is_supported(family, type, proto)) {
        printf("[ERR] qc7xx_socket_create: Unsupported socket parameters (family=%d, type=%d, proto=%d)\r\n",
                family, type, proto);
        errno = EAFNOSUPPORT;
        return -1;
    }

    /* Reserve a file descriptor */
    fd = zvfs_reserve_fd();
    if (fd < 0) {
        printf("[ERR] qc7xx_socket_create: Failed to reserve file descriptor (errno=%d)\r\n", errno);
        return -1;
    }
    
    /* Create socket via IPC to NP core */
    np_sockfd = qc7xx_ipc_socket_create(family, type, proto);
    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_create: Failed to create NP socket (ret=%d, errno=%d)\r\n",
                np_sockfd, -np_sockfd);
        zvfs_free_fd(fd);
        errno = -np_sockfd;
        return -1;
    }
    
    /* Set up socket object */
    sock_obj = SD_TO_OBJ(np_sockfd);

    /* Finalize the file descriptor */
    zvfs_finalize_typed_fd(fd, sock_obj,
                            (const struct fd_op_vtable *)&qc7xx_socket_fd_op_vtable,
                            ZVFS_MODE_IFSOCK);

    return fd;
}

static ssize_t qc7xx_socket_read(void *obj, void *buf, size_t len)
{
    int np_sockfd = OBJ_TO_SD(obj);
    ssize_t ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_read: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    if (!buf || len == 0) {
        printf("[ERR] qc7xx_socket_read: Invalid buffer (buf=%p, len=%zu)\r\n", buf, len);
        errno = EINVAL;
        return -1;
    }

    ret = qc7xx_ipc_socket_recvfrom(np_sockfd, buf, len, 0, NULL, NULL);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_read: Failed to read from NP socket %d (ret=%zd, errno=%d)\r\n",
                np_sockfd, ret, errno);
    }

    return ret;
}

static ssize_t qc7xx_socket_write(void *obj, const void *buf, size_t len)
{
    int np_sockfd = OBJ_TO_SD(obj);
    ssize_t ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_write: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    if (!buf || len == 0) {
        printf("[ERR] qc7xx_socket_write: Invalid buffer (buf=%p, len=%zu)\r\n", buf, len);
        errno = EINVAL;
        return -1;
    }

    ret = qc7xx_ipc_socket_sendto(np_sockfd, buf, len, 0, NULL, 0);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_write: Failed to write to NP socket %d (ret=%zd, errno=%d)\r\n",
                np_sockfd, ret, errno);
    }

    return ret;
}

static int qc7xx_socket_close(void *obj)
{
    int np_sockfd = OBJ_TO_SD(obj);
    int ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_close: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    ret = qc7xx_ipc_socket_close(np_sockfd);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_close: Failed to close NP socket %d (ret=%d, errno=%d)\r\n",
                np_sockfd, ret, errno);
    } else {
    }

    return ret;
}

static int qc7xx_socket_bind(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);
    int ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_bind: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    if (!addr || addrlen == 0) {
        printf("[ERR] qc7xx_socket_bind: Invalid address (addr=%p, addrlen=%d)\r\n", addr, addrlen);
        errno = EINVAL;
        return -1;
    }

    ret = qc7xx_ipc_socket_bind(np_sockfd, addr, addrlen);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_bind: Failed to bind NP socket %d (ret=%d, errno=%d)\r\n",
                np_sockfd, ret, errno);
    } else {
    }

    return ret;
}

static int qc7xx_socket_connect(void *obj, const struct sockaddr *addr, socklen_t addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);
    int ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_connect: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    if (!addr || addrlen == 0) {
        printf("[ERR] qc7xx_socket_connect: Invalid address (addr=%p, addrlen=%d)\r\n", addr, addrlen);
        errno = EINVAL;
        return -1;
    }

    ret = qc7xx_ipc_socket_connect(np_sockfd, addr, addrlen);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_connect: Failed to connect NP socket %d (ret=%d, errno=%d)\r\n",
                np_sockfd, ret, errno);
    } else {
    }

    return ret;
}

static int qc7xx_socket_listen(void *obj, int backlog)
{
    int np_sockfd = OBJ_TO_SD(obj);
    int ret;


    if (np_sockfd < 0) {
        printf("[ERR] qc7xx_socket_listen: Invalid socket object (np_sockfd=%d)\r\n", np_sockfd);
        errno = EBADF;
        return -1;
    }

    if (backlog < 0) {
        printf("[ERR] qc7xx_socket_listen: Invalid backlog=%d\r\n", backlog);
        errno = EINVAL;
        return -1;
    }

    ret = qc7xx_ipc_socket_listen(np_sockfd, backlog);
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_listen: Failed to listen on NP socket %d (ret=%d, errno=%d)\r\n",
                np_sockfd, ret, errno);
    }

    return ret;
}

static int qc7xx_socket_accept(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);
    int new_np_sockfd;
    int new_fd;
    void *new_obj;


    new_np_sockfd = qc7xx_ipc_socket_accept(np_sockfd, addr, addrlen);
    if (new_np_sockfd < 0) {
        return new_np_sockfd;
    }

    /* Reserve new file descriptor */
    new_fd = zvfs_reserve_fd();
    if (new_fd < 0) {
        qc7xx_ipc_socket_close(new_np_sockfd);
        errno = ENFILE;
        return -1;
    }

    /* Set up new socket object */
    new_obj = SD_TO_OBJ(new_np_sockfd);

    /* Finalize the new file descriptor */
    zvfs_finalize_typed_fd(new_fd, new_obj,
                            (const struct fd_op_vtable *)&qc7xx_socket_fd_op_vtable,
                            ZVFS_MODE_IFSOCK);


    return new_fd;
}

static ssize_t qc7xx_socket_sendto(void *obj, const void *buf, size_t len, int flags,
                                   const struct sockaddr *dest_addr, socklen_t addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!buf || len == 0) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_sendto(np_sockfd, buf, len, flags, dest_addr, addrlen);
}

static ssize_t qc7xx_socket_recvfrom(void *obj, void *buf, size_t max_len, int flags,
                                     struct sockaddr *src_addr, socklen_t *addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!buf || max_len == 0) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_recvfrom(np_sockfd, buf, max_len, flags, src_addr, addrlen);
}

static int qc7xx_socket_getsockopt(void *obj, int level, int optname,
                                   void *optval, socklen_t *optlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!optval || !optlen) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_getsockopt(np_sockfd, level, optname, optval, optlen);
}

static int qc7xx_socket_setsockopt(void *obj, int level, int optname,
                                   const void *optval, socklen_t optlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!optval || optlen == 0) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_setsockopt(np_sockfd, level, optname, optval, optlen);
}

static int qc7xx_socket_getsockname(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!addr || !addrlen) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_getsockname(np_sockfd, addr, addrlen);
}

static int qc7xx_socket_getpeername(void *obj, struct sockaddr *addr, socklen_t *addrlen)
{
    int np_sockfd = OBJ_TO_SD(obj);

    if (!addr || !addrlen) {
        errno = EINVAL;
        return -1;
    }


    return qc7xx_ipc_socket_getpeername(np_sockfd, addr, addrlen);
}

static int qc7xx_socket_shutdown(void *obj, int how)
{
    int np_sockfd = OBJ_TO_SD(obj);


    return qc7xx_ipc_socket_shutdown(np_sockfd, how);
}

int qc7xx_socket_offload_init(struct net_if *iface)
{
    int ret;


    /* Verify RPMsg is initialized */
    if (!qc7xx_rpmsg_is_initialized()) {
        printf("[ERR] qc7xx_socket_offload_init: RPMsg not initialized, cannot initialize socket offload\r\n");
        return -ENODEV;
    }

    /* Initialize IPC socket layer */
    ret = qc7xx_ipc_socket_init();
    if (ret < 0) {
        printf("[ERR] qc7xx_socket_offload_init: Failed to initialize IPC socket layer: %d\r\n", ret);
        return ret;
    }

    /* Register DNS offload */
    socket_offload_dns_register(&qc7xx_dns_offload);


    return 0;
}

/* Register the socket offload implementation */
NET_SOCKET_OFFLOAD_REGISTER(qc7xx_ap, CONFIG_QC7XX_SOCKET_OFFLOAD_PRIORITY,
                            AF_UNSPEC, qc7xx_socket_is_supported, qc7xx_socket_create);
