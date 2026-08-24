/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>
#include <metal/device.h>
#include <openamp/open_amp.h>
#include <openamp/rpmsg_rpc_client_server.h>

#include "qc7xx_ipc_socket.h"


/* RPMsg RPC configuration */
#define QC7XX_RPC_SERVICE_NAME         "socket-rpc-service"
#define QC7XX_RPC_TIMEOUT_MS           5000
#define QC7XX_RPC_MAX_SOCKETS          64

/* Global RPC client instance */
static struct rpmsg_rpc_clt g_rpc_client;
static struct rpmsg_device *g_rpmsg_dev = NULL;
static bool g_rpc_initialized = false;

/* RPC response management */
static struct k_mutex rpc_ctx_mutex;
static struct qc7xx_rpc_ctx g_rpc_ctx;

/* Socket file descriptor tracking */
static int socket_fd_table[QC7XX_RPC_MAX_SOCKETS];
static K_MUTEX_DEFINE(fd_table_mutex);

/* Allocate a socket file descriptor */
static int alloc_socket_fd(void)
{
    int fd;

    k_mutex_lock(&fd_table_mutex, K_FOREVER);

    for (fd = 0; fd < QC7XX_RPC_MAX_SOCKETS; fd++) {
        if (socket_fd_table[fd] == -1) {
            socket_fd_table[fd] = fd; /* Mark as used */
            k_mutex_unlock(&fd_table_mutex);
            return fd;
        }
    }

    k_mutex_unlock(&fd_table_mutex);
    return -EMFILE; /* Too many open files */
}

/* Free a socket file descriptor */
static void free_socket_fd(int fd)
{
    if (fd >= 0 && fd < QC7XX_RPC_MAX_SOCKETS) {
        k_mutex_lock(&fd_table_mutex, K_FOREVER);
        socket_fd_table[fd] = -1;
        k_mutex_unlock(&fd_table_mutex);
    }
}

/* Get NP core socket FD from AP core FD */
static int get_np_sockfd(int ap_sockfd)
{
    int np_sockfd;

    if (ap_sockfd < 0 || ap_sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EBADF;
    }

    k_mutex_lock(&fd_table_mutex, K_FOREVER);
    np_sockfd = socket_fd_table[ap_sockfd];
    k_mutex_unlock(&fd_table_mutex);

    return np_sockfd < 0 ? -EBADF : np_sockfd;
}

/* Store NP core socket FD */
static int store_np_sockfd(int ap_sockfd, int np_sockfd)
{
    if (ap_sockfd < 0 || ap_sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EBADF;
    }

    k_mutex_lock(&fd_table_mutex, K_FOREVER);
    socket_fd_table[ap_sockfd] = np_sockfd;
    k_mutex_unlock(&fd_table_mutex);

    return 0;
}

/* RPC response callback function */
void qc7xx_rpc_callback(struct rpmsg_rpc_clt *rpc, int status, void *data, size_t len)
{
    ARG_UNUSED(rpc);

    struct rpc_response *resp = (struct rpc_response *)data;

    k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);

    if (len > 0 && data) {
        size_t copy_len = MIN(len, sizeof(g_rpc_ctx.data));
        resp->data[QC7XX_RPC_MAX_BUF_LEN-1] = "\0";
        g_rpc_ctx.result = resp->result;
        g_rpc_ctx.errno_val = resp->errno_val;
        g_rpc_ctx.data_len = resp->data_len;
        memcpy(&g_rpc_ctx.data, resp->data, copy_len);
        g_rpc_ctx.completed = true;
    } else {
        //printf("[WRN] RPC callback: no data received\r\n");
    }

    k_mutex_unlock(&rpc_ctx_mutex);
    k_sem_give(&g_rpc_ctx.sem);
}

/* Send RPC request and wait for response */
static int send_rpc_request(uint32_t rpc_id, void *request_data, size_t request_size)
{
    int ret;

    if (!g_rpc_initialized) {
        printf("[ERR] RPC client not initialized\r\n");
        return -ENOTCONN;
    }

    /* Initialize context */
    k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);
    memset(&g_rpc_ctx, 0, sizeof(g_rpc_ctx));
    k_sem_init(&g_rpc_ctx.sem, 0, 1);
    k_mutex_unlock(&rpc_ctx_mutex);

    /* Send RPC request */

    ret = rpmsg_rpc_client_send(&g_rpc_client, rpc_id, request_data, request_size);
    if (ret < 0) {
        printf("[ERR] Failed to send RPC request: id=%u, ret=%d\r\n", rpc_id, ret);
        return ret;
    }
    /* Wait for response with timeout */
    ret = k_sem_take(&g_rpc_ctx.sem, K_MSEC(QC7XX_RPC_TIMEOUT_MS));
    if (ret < 0) {
        printf("[ERR] RPC request timeout\r\n");
        return -ETIMEDOUT;
    }

    k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);
    if (!g_rpc_ctx.completed) {
        k_mutex_unlock(&rpc_ctx_mutex);
        return -ETIMEDOUT;
    }

    ret = g_rpc_ctx.result;
    if (ret < 0) {
        errno = g_rpc_ctx.errno_val;
    }
    k_mutex_unlock(&rpc_ctx_mutex);
    return ret;
}

/* RPC client service table */
static const struct rpmsg_rpc_client_services qc7xx_socket_rpc_services[] = {
    {
        .id = QC7XX_RPC_SOCKET_CREATE,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_CLOSE,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_BIND,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_CONNECT,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_LISTEN,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_ACCEPT,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_SEND,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_SENDTO,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_RECV,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_RECVFROM,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_SETSOCKOPT,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_GETSOCKOPT,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_GETSOCKNAME,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_GETPEERNAME,
        .cb = qc7xx_rpc_callback,
    },
    {
        .id = QC7XX_RPC_SOCKET_SHUTDOWN,
        .cb = qc7xx_rpc_callback,
    },
};


void qc7xx_rpc_client_shutdown(void)
{
    /* This function is now handled by qc7xx_rpmsg_init.c */
    /* Keeping stub for compatibility */
}

int qc7xx_ipc_socket_init(void)
{
    /* Note: The actual RPMsg device initialization should be done
     * in the platform-specific code and passed to qc7xx_rpc_client_init()
     */
    return 0;
}

int qc7xx_ipc_socket_create(int family, int type, int proto)
{
    struct qc7xx_rpc_socket_create params;
    int ap_fd, np_fd, ret;

    /* Allocate AP core socket FD */
    ap_fd = alloc_socket_fd();
    if (ap_fd < 0) {
        return ap_fd;
    }

    /* Prepare parameters */
    params.family = family;
    params.type = type;
    params.proto = proto;
    /* Send RPC request */
    ret = send_rpc_request(QC7XX_RPC_SOCKET_CREATE, &params, sizeof(params));
    if (ret < 0) {
        free_socket_fd(ap_fd);
        return ret;
    }

    np_fd = ret;
    if (np_fd < 0) {
        free_socket_fd(ap_fd);
        errno = -np_fd;
        return -1;
    }

    /* Store NP core socket FD */
    store_np_sockfd(ap_fd, np_fd);
    return ap_fd;
}

int qc7xx_ipc_socket_close(int sockfd)
{
    int np_sockfd, ret;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EBADF;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    /* Send RPC request */
    ret = send_rpc_request(QC7XX_RPC_SOCKET_CLOSE, &np_sockfd, sizeof(np_sockfd));

    /* Free the AP core socket FD regardless of result */
    free_socket_fd(sockfd);

    return ret;
}

int qc7xx_ipc_socket_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    struct qc7xx_rpc_socket_bind *params;
    int np_sockfd, ret;
    size_t params_size;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !addr || addrlen == 0) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params_size = sizeof(*params) + addrlen - sizeof(params->addr);
    params = k_malloc(params_size);
    if (!params) {
        return -ENOMEM;
    }

    params->sockfd = np_sockfd;
    params->addrlen = addrlen;
    memcpy(&params->addr, addr, addrlen);

    ret = send_rpc_request(QC7XX_RPC_SOCKET_BIND, params, params_size);

    k_free(params);
    return ret;
}

int qc7xx_ipc_socket_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen)
{
    struct qc7xx_rpc_socket_connect *params;
    int np_sockfd, ret;
    size_t params_size;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !addr || addrlen == 0) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params_size = sizeof(*params) + addrlen - sizeof(params->addr);
    params = k_malloc(params_size);
    if (!params) {
        return -ENOMEM;
    }

    params->sockfd = np_sockfd;
    params->addrlen = addrlen;
    memcpy(&params->addr, addr, addrlen);

    ret = send_rpc_request(QC7XX_RPC_SOCKET_CONNECT, params, params_size);

    k_free(params);
    return ret;
}

int qc7xx_ipc_socket_listen(int sockfd, int backlog)
{
    struct qc7xx_rpc_socket_listen params;
    int np_sockfd, ret;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params.sockfd = np_sockfd;
    params.backlog = backlog;

    ret = send_rpc_request(QC7XX_RPC_SOCKET_LISTEN, &params, sizeof(params));
    return ret;
}

int qc7xx_ipc_socket_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    struct qc7xx_rpc_socket_accept params;
    int np_sockfd, new_np_sockfd, new_ap_fd, ret;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    /* Allocate new AP core socket FD */
    new_ap_fd = alloc_socket_fd();
    if (new_ap_fd < 0) {
        return new_ap_fd;
    }

    params.sockfd = np_sockfd;
    params.addrlen = (addr && addrlen) ? *addrlen : 0;

    ret = send_rpc_request(QC7XX_RPC_SOCKET_ACCEPT, &params, sizeof(params));
    if (ret < 0) {
        free_socket_fd(new_ap_fd);
        return ret;
    }

    new_np_sockfd = ret;
    if (new_np_sockfd < 0) {
        free_socket_fd(new_ap_fd);
        errno = -new_np_sockfd;
        return -1;
    }

    /* Store new NP core socket FD */
    store_np_sockfd(new_ap_fd, new_np_sockfd);

    /* Copy peer address if requested */
    if (addr && addrlen) {
        k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);
        if (g_rpc_ctx.addrlen > *addrlen) {
            g_rpc_ctx.addrlen = *addrlen;
        }
        memcpy(addr, g_rpc_ctx.data, g_rpc_ctx.addrlen);
        *addrlen = g_rpc_ctx.addrlen;
        k_mutex_unlock(&rpc_ctx_mutex);
    }

    return new_ap_fd;
}

ssize_t qc7xx_ipc_socket_sendto(int sockfd, const void *buf, size_t len, int flags,
                                const struct sockaddr *dest_addr, socklen_t addrlen)
{
    struct qc7xx_rpc_socket_sendto *params;
    int np_sockfd, ret;
    size_t params_size;
    uint8_t *data_ptr;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !buf || len == 0) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    /* Check if data fits in RPC buffer */
    params_size = sizeof(*params) + len;
    if (dest_addr && addrlen > 0) {
        params_size += addrlen;
    }

    if (params_size > QC7XX_RPC_MAX_BUF_LEN) {
        return -EMSGSIZE;
    }

    params = k_malloc(params_size);
    if (!params) {
        return -ENOMEM;
    }

    params->sockfd = np_sockfd;
    params->flags = flags;
    params->len = len;
    params->addrlen = (dest_addr && addrlen > 0) ? addrlen : 0;

    data_ptr = (uint8_t *)&params->data;

    /* Copy destination address if provided */
    if (dest_addr && addrlen > 0) {
        memcpy(data_ptr, dest_addr, addrlen);
        data_ptr += addrlen;
    }

    /* Copy data to send */
    memcpy(data_ptr, buf, len);

    ret = send_rpc_request(QC7XX_RPC_SOCKET_SENDTO, params, params_size);

    k_free(params);
    return ret;
}

ssize_t qc7xx_ipc_socket_send(int sockfd, const void *buf, size_t len, int flags)
{
    return qc7xx_ipc_socket_sendto(sockfd, buf, len, flags, NULL, 0);
}

ssize_t qc7xx_ipc_socket_recvfrom(int sockfd, void *buf, size_t len, int flags,
                                  struct sockaddr *src_addr, socklen_t *addrlen)
{
    struct qc7xx_rpc_socket_recvfrom params;
    int np_sockfd, ret;
    size_t bytes_to_copy;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !buf || len == 0) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params.sockfd = np_sockfd;
    params.flags = flags;
    params.len = len;
    params.addrlen = (src_addr && addrlen) ? *addrlen : 0;

    ret = send_rpc_request(QC7XX_RPC_SOCKET_RECVFROM, &params, sizeof(params));
    if (ret < 0) {
        return ret;
    }

    /* Copy received data */
    k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);
    bytes_to_copy = MIN(len, (size_t)g_rpc_ctx.data_len);
    memcpy(buf, g_rpc_ctx.data, bytes_to_copy);

    /* Copy source address if requested */
    if (src_addr && addrlen) {
        if (g_rpc_ctx.addrlen > *addrlen) {
            g_rpc_ctx.addrlen = *addrlen;
        }
        memcpy(src_addr, g_rpc_ctx.data + bytes_to_copy, g_rpc_ctx.addrlen);
        *addrlen = g_rpc_ctx.addrlen;
    }
    k_mutex_unlock(&rpc_ctx_mutex);

    return bytes_to_copy;
}

ssize_t qc7xx_ipc_socket_recv(int sockfd, void *buf, size_t len, int flags)
{
    return qc7xx_ipc_socket_recvfrom(sockfd, buf, len, flags, NULL, NULL);
}

int qc7xx_ipc_socket_setsockopt(int sockfd, int level, int optname,
                               const void *optval, socklen_t optlen)
{
    struct qc7xx_rpc_socket_setsockopt *params;
    int np_sockfd, ret;
    size_t params_size;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !optval || optlen == 0) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params_size = sizeof(*params) + optlen;
    if (params_size > QC7XX_RPC_MAX_BUF_LEN) {
        return -EMSGSIZE;
    }

    params = k_malloc(params_size);
    if (!params) {
        return -ENOMEM;
    }

    params->sockfd = np_sockfd;
    params->level = level;
    params->optname = optname;
    params->optlen = optlen;
    memcpy(params->data, optval, optlen);

    ret = send_rpc_request(QC7XX_RPC_SOCKET_SETSOCKOPT, params, params_size);

    k_free(params);
    return ret;
}

int qc7xx_ipc_socket_getsockopt(int sockfd, int level, int optname,
                               void *optval, socklen_t *optlen)
{
    struct qc7xx_rpc_socket_getsockopt params;
    int np_sockfd, ret;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS || !optval || !optlen) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    params.sockfd = np_sockfd;
    params.level = level;
    params.optname = optname;
    params.optlen = *optlen;

    ret = send_rpc_request(QC7XX_RPC_SOCKET_GETSOCKOPT, &params, sizeof(params));
    if (ret < 0) {
        return ret;
    }

    /* Copy option value */
    k_mutex_lock(&rpc_ctx_mutex, K_FOREVER);
    if (g_rpc_ctx.optlen > *optlen) {
        g_rpc_ctx.optlen = *optlen;
    }
    memcpy(optval, g_rpc_ctx.data, g_rpc_ctx.optlen);
    *optlen = g_rpc_ctx.optlen;
    k_mutex_unlock(&rpc_ctx_mutex);

    return 0;
}

int qc7xx_ipc_socket_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Note: This would require additional RPC service implementation */
    return -ENOTSUP;
}

int qc7xx_ipc_socket_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen)
{
    /* Note: This would require additional RPC service implementation */
    return -ENOTSUP;
}

int qc7xx_ipc_socket_shutdown(int sockfd, int how)
{
    int np_sockfd, ret;

    if (sockfd < 0 || sockfd >= QC7XX_RPC_MAX_SOCKETS) {
        return -EINVAL;
    }

    np_sockfd = get_np_sockfd(sockfd);
    if (np_sockfd < 0) {
        return -EBADF;
    }

    ret = send_rpc_request(QC7XX_RPC_SOCKET_SHUTDOWN, &how, sizeof(how));
    return ret;
}

/* RPC client initialization and management functions */

/**
 * @brief Initialize RPMsg RPC client for socket offload
 *
 * This function creates the RPMsg device and initializes the RPC client
 * to communicate with the network core.
 *
 * @return 0 on success, negative errno on failure
 */
int qc7xx_rpmsg_and_rpc_init(void)
{
    int ret;

    if (g_rpc_initialized) {
        return 0;
    }


    /* Create RPMsg device - platform specific */
    extern struct rpmsg_device *platform_create_rpmsg_vdev(void);
    struct rpmsg_device *rdev = platform_create_rpmsg_vdev();
    if (!rdev) {
        printf("[ERR] Failed to create RPMsg device\r\n");
        return -ENODEV;
    }

    /* Save the RPMsg device */
    g_rpmsg_dev = rdev;

    /* Initialize RPC client */
    ret = rpmsg_rpc_client_init(&g_rpc_client, rdev,
                                NULL, /* shutdown callback */
                                qc7xx_socket_rpc_services,
                                ARRAY_SIZE(qc7xx_socket_rpc_services));
    if (ret < 0) {
        printf("[ERR] Failed to initialize RPC client: %d\r\n", ret);
        return ret;
    }

    /* Initialize mutex */
    k_mutex_init(&rpc_ctx_mutex);

    /* Initialize socket FD table */
    for (int i = 0; i < QC7XX_RPC_MAX_SOCKETS; i++) {
        socket_fd_table[i] = -1;
    }

    g_rpc_initialized = true;

    return 0;
}

/**
 * @brief Shutdown RPMsg RPC client
 */
void qc7xx_rpmsg_and_rpc_shutdown(void)
{
    if (!g_rpc_initialized) {
        return;
    }


    /* Check if RPC client is valid before releasing */
    /* Note: Use g_rpc_initialized flag instead of struct member */
    rpmsg_rpc_client_release(&g_rpc_client);

    g_rpmsg_dev = NULL;
    g_rpc_initialized = false;

}

/**
 * @brief Check if RPMsg RPC client is initialized
 *
 * @return true if initialized, false otherwise
 */
bool qc7xx_rpmsg_is_initialized(void)
{
    return g_rpc_initialized;
}

/**
 * @brief Get RPMsg device instance
 *
 * @return RPMsg device pointer or NULL if not initialized
 */
struct rpmsg_device *qc7xx_get_rpmsg_device(void)
{
    return g_rpmsg_dev;
}

/**
 * @brief Check if RPC client is initialized
 *
 * @return true if initialized, false otherwise
 */
bool qc7xx_ipc_socket_is_initialized(void)
{
    return g_rpc_initialized;
}
