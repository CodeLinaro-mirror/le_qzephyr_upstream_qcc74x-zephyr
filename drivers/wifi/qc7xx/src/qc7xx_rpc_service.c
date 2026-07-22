/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include "qc7xx_rpc_service.h"
#include "qc7xx_ipc_socket.h"

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/net_if.h>
#include <zephyr/device.h>
#include <zephyr/ipc/rpmsg_service.h>
#include <openamp/open_amp.h>
#include <openamp/rpmsg_rpc_client_server.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

/* RPC service configuration */
#define RPC_SERVICE_NAME           "socket-rpc-service"
#define MAX_SOCKETS               64
//#define RPC_BUFFER_SIZE          QC7XX_RPC_MAX_BUF_LEN 
#define RPC_BUFFER_SIZE             1536
#define SOCKET_FD_BASE            1000

/* Socket tracking structure */
struct socket_entry {
    int fd;                          /* Client FD (returned to AP core) */
    int real_fd;                     /* Real socket FD on NP core */
    int family;
    int type;
    int protocol;
    bool is_used;
    bool is_connected;
    bool is_bound;
    struct sockaddr_storage local_addr;
    struct sockaddr_storage remote_addr;
    struct k_mutex lock;
};

/* RPC server state */
struct rpc_server_state {
    struct rpmsg_rpc_svr rpc_server;
    struct socket_entry sockets[MAX_SOCKETS];
    struct k_mutex socket_table_mutex;
    int next_fd;
    bool initialized;
};

static struct rpc_server_state g_rpc_server;

/* RPC message structures - use definitions from qc7xx_ipc_socket.h */
#if 0
/* RPC response structure */
struct rpc_response {
    int result;
    int errno_val;
    size_t data_len;
    uint8_t data[RPC_BUFFER_SIZE];
};
#endif
/* Simple errno to string mapping for debugging */
static const char *errno_to_str(int err)
{
    switch (err) {
    case 121: return "EREMOTEIO (Remote I/O error)";
    case 11: return "EAGAIN (Resource temporarily unavailable)";
    case 22: return "EINVAL (Invalid argument)";
    case 9: return "EBADF (Bad file descriptor)";
    case 111: return "ECONNREFUSED (Connection refused)";
    case 113: return "EHOSTUNREACH (No route to host)";
    default: return "Unknown error";
    }
}

/**
 * @brief Allocate a socket file descriptor
 */
static int alloc_socket_fd(int family, int type, int protocol)
{
    int i, fd;

    k_mutex_lock(&g_rpc_server.socket_table_mutex, K_FOREVER);

    for (i = 0; i < MAX_SOCKETS; i++) {
        if (!g_rpc_server.sockets[i].is_used) {
            fd = g_rpc_server.next_fd++;

            g_rpc_server.sockets[i].fd = fd;
            g_rpc_server.sockets[i].real_fd = -1;  /* Will be set when real socket is created */
            g_rpc_server.sockets[i].family = family;
            g_rpc_server.sockets[i].type = type;
            g_rpc_server.sockets[i].protocol = protocol;
            g_rpc_server.sockets[i].is_used = true;
            g_rpc_server.sockets[i].is_connected = false;
            g_rpc_server.sockets[i].is_bound = false;

            k_mutex_init(&g_rpc_server.sockets[i].lock);

            k_mutex_unlock(&g_rpc_server.socket_table_mutex);
            return fd;
        }
    }

    k_mutex_unlock(&g_rpc_server.socket_table_mutex);
    return -EMFILE;
}

/**
 * @brief Free a socket file descriptor
 */
static int free_socket_fd(int fd)
{
    int i;

    k_mutex_lock(&g_rpc_server.socket_table_mutex, K_FOREVER);

    for (i = 0; i < MAX_SOCKETS; i++) {
        if (g_rpc_server.sockets[i].fd == fd && g_rpc_server.sockets[i].is_used) {
            k_mutex_lock(&g_rpc_server.sockets[i].lock, K_FOREVER);

            /* close(fd); */ /* Commented out for now */

            g_rpc_server.sockets[i].is_used = false;
            g_rpc_server.sockets[i].is_connected = false;
            g_rpc_server.sockets[i].is_bound = false;

            g_rpc_server.next_fd = SOCKET_FD_BASE;
            k_mutex_unlock(&g_rpc_server.sockets[i].lock);

            k_mutex_unlock(&g_rpc_server.socket_table_mutex);
            return 0;
        }
    }

    k_mutex_unlock(&g_rpc_server.socket_table_mutex);
    return -EBADF;
}

/**
 * @brief Find socket entry by file descriptor
 */
static struct socket_entry *find_socket_entry(int fd)
{
    int i;

    k_mutex_lock(&g_rpc_server.socket_table_mutex, K_FOREVER);

    for (i = 0; i < MAX_SOCKETS; i++) {
        if (g_rpc_server.sockets[i].fd == fd && g_rpc_server.sockets[i].is_used) {
            struct socket_entry *entry = &g_rpc_server.sockets[i];
            k_mutex_unlock(&g_rpc_server.socket_table_mutex);
            return entry;
        }
    }

    k_mutex_unlock(&g_rpc_server.socket_table_mutex);
    return NULL;
}

/**
 * @brief Get real socket FD from client FD
 */
static int get_real_socket_fd(int client_fd)
{
    struct socket_entry *entry = find_socket_entry(client_fd);
    if (!entry) {
        return -EBADF;
    }
    return entry->real_fd;
}

/**
 * @brief Update real socket FD mapping
 */
static int update_real_socket_fd(int client_fd, int real_fd)
{
    struct socket_entry *entry = find_socket_entry(client_fd);
    if (!entry) {
        return -EBADF;
    }

    k_mutex_lock(&entry->lock, K_FOREVER);
    entry->real_fd = real_fd;
    k_mutex_unlock(&entry->lock);

    printf("[DBG] NP: Mapped client_fd=%d to real_fd=%d\r\n", client_fd, real_fd);
    return 0;
}

/**
 * @brief Send RPC response
 */
static int send_rpc_response(struct rpmsg_rpc_svr *rpcs, uint32_t rpc_id,
                            int result, int errno_val, const void *data, size_t data_len)
{
    struct rpc_response response;

    printf("[DBG] NP: Sending RPC response (rpc_id=%u, result=%d, errno=%d, data_len=%zu)\r\n",
           rpc_id, result, errno_val, data_len);

    response.result = result;
    response.errno_val = errno_val;
    response.data_len = data_len;

    if (data && data_len > 0) {
        size_t copy_len = MIN(data_len, sizeof(response.data));
        memcpy(response.data, data, copy_len);
    }
    //do some well func
    int ret =  rpmsg_rpc_server_send(rpcs, rpc_id, RPMSG_RPC_OK, &response,
                                 sizeof(response.result) + sizeof(response.errno_val) +
                                 sizeof(response.data_len) + response.data_len);

    if (ret < 0) {
        printf("[ERR] NP: Failed to send RPC response (rpc_id=%u, ret=%d)\r\n", rpc_id, ret);
    } else {
        printf("[DBG] NP: RPC response sent successfully (rpc_id=%u, bytes=%d)\r\n", rpc_id, ret);
    }

    return ret < 0 ?  ret : 0;
}

/**
 * @brief Handle socket creation RPC
 */
static int handle_socket_create(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_create] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_create *req = (struct qc7xx_rpc_socket_create *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    int real_fd, client_fd;

    /* Extract socket parameters */
    int family = req->family;
    int type = req->type;
    int proto = req->proto;

    real_fd = -1; /* Simulate socket creation - actual implementation needed */
    /* real_fd = socket(family, type, proto);
    if (real_fd < 0) {
        return send_rpc_response(rpcs, rpc_id, -1, errno, NULL, 0);
    } */

    client_fd = alloc_socket_fd(family, type, proto);
    if (client_fd < 0) {
        printf("[ERR] NP: Failed to allocate socket FD\r\n");
        //zsock_close(real_fd);
        printf("[NP_RPC] Cleaned up real_fd=%d due to client_fd allocation failure\r\n", real_fd);
        return send_rpc_response(rpcs, rpc_id, -1, EMFILE, NULL, 0);
    }

    /* Get socket entry for this client_fd */
    struct socket_entry *entry = find_socket_entry(client_fd);
    if (!entry) {
        printf("[ERR] NP: Failed to find socket entry for client_fd=%d\r\n", client_fd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    /* Create real socket on NP core using Zephyr socket API */
    real_fd = zsock_socket(family, type, proto);
    if (real_fd < 0) {
        printf("[ERR] NP: Failed to create real socket: errno=%d\r\n", errno);
        /* Clean up the allocated client FD */
        k_mutex_lock(&entry->lock, K_FOREVER);
        free_socket_fd(client_fd);
        k_mutex_unlock(&entry->lock);
        return send_rpc_response(rpcs, rpc_id, -1, errno, NULL, 0);
    }

    /* Map client_fd to real_fd */
    int map_ret = update_real_socket_fd(client_fd, real_fd);
    if (map_ret < 0) {
        printf("[ERR] NP: Failed to map FDs: ret=%d\r\n", map_ret);
        zsock_close(real_fd);
        k_mutex_lock(&entry->lock, K_FOREVER);
        free_socket_fd(client_fd);
        k_mutex_unlock(&entry->lock);
        return send_rpc_response(rpcs, rpc_id, -1, map_ret, NULL, 0);
    }

    printf("[DBG] NP: Socket created successfully (client_fd=%d, real_fd=%d)\r\n",
           client_fd, real_fd);

    int resp_ret = send_rpc_response(rpcs, rpc_id, client_fd, 0, NULL, 0);
    printf("[DBG] NP: Sent socket create response (rpc_id=%u, fd=%d, ret=%d)\r\n",
           rpc_id, client_fd, resp_ret);

    return resp_ret;
}

/**
 * @brief Handle socket close RPC
 */
static int handle_socket_close(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [int fd] */
    uint32_t rpc_id = *(uint32_t *)data;
    int fd = *(int *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    int ret = 0;

    entry = find_socket_entry(fd);
    if (!entry) {
        printf("[ERR] NP: Socket close failed - fd not found (fd=%d)\r\n", fd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Get and close the real socket */
    if (entry->real_fd >= 0) {
        ret = zsock_close(entry->real_fd);
        if (ret < 0) {
            printf("[ERR] NP: Failed to close real socket (real_fd=%d): errno=%d\r\n",
                   entry->real_fd, errno);
        } else {
            printf("[DBG] NP: Real socket closed successfully (real_fd=%d)\r\n",
                   entry->real_fd);
        }
        entry->real_fd = -1;
    } else {
        printf("[WRN] NP: No real socket to close for client_fd=%d\r\n", fd);
    }

    free_socket_fd(fd);
    k_mutex_unlock(&entry->lock);

    printf("[DBG] NP: Socket closed successfully (fd=%d)\r\n", fd);
    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket connect RPC
 */
static int handle_socket_connect(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_connect] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_connect *req = (struct qc7xx_rpc_socket_connect *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    int ret;

    if (req->addr.sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&req->addr;
        char ip_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
        printf("[INF] NP: Target Address: %s:%d (IPv4)\r\n", ip_str, ntohs(addr_in->sin_port));
    }

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket connect failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call connect on the real socket */
    ret = zsock_connect(entry->real_fd, (struct sockaddr *)&req->addr, req->addrlen);
    if (ret < 0) {
        printf("[ERR] NP: Failed to connect real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Socket connected successfully (fd=%d, real_fd=%d)\r\n", req->sockfd, entry->real_fd);
        entry->is_connected = true;
        memcpy(&entry->remote_addr, &req->addr, req->addrlen);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket bind RPC
 */
static int handle_socket_bind(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_bind] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_bind *req = (struct qc7xx_rpc_socket_bind *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    int ret;

    if (req->addr.sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&req->addr;
        char ip_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
        printf("[INF] NP: Address: %s:%d (IPv4)\r\n", ip_str, ntohs(addr_in->sin_port));
    }

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    if (req->addr.sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)&req->addr;
        char ip_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
        printf("[DBG] NP:   bind_addr_raw: 0x%02X%02X%02X%02X\r\n",
               addr_in->sin_addr.s_addr & 0xFF,
               (addr_in->sin_addr.s_addr >> 8) & 0xFF,
               (addr_in->sin_addr.s_addr >> 16) & 0xFF,
               (addr_in->sin_addr.s_addr >> 24) & 0xFF);
    }

    ret = zsock_bind(entry->real_fd, (struct sockaddr *)&req->addr, req->addrlen);

    if (ret < 0) {
        printf("[ERR] NP: Failed to bind real socket (real_fd=%d): errno=%d (%s)\r\n",
               entry->real_fd, errno, strerror(errno));
    } else {
        printf("[DBG] NP: Socket bound successfully (fd=%d, real_fd=%d)\r\n",
               req->sockfd, entry->real_fd);
        entry->is_bound = true;
        memcpy(&entry->local_addr, &req->addr, req->addrlen);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket send RPC
 */
static int handle_socket_send(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_send] + data */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_send *req = (struct qc7xx_rpc_socket_send *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char *send_data;
    int ret;

    /* Data starts after the header struct */
    send_data = (char *)((uint8_t *)data + 4 + sizeof(struct qc7xx_rpc_socket_send));
    if (send_data && req->len > 0) {
        printf("[INF] NP: Data: %.*s\r\n", (int)MIN(req->len, 64), send_data);
    }

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    if (!entry->is_connected) {
        return send_rpc_response(rpcs, rpc_id, -1, ENOTCONN, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call send on the real socket */
    ret = zsock_send(entry->real_fd, send_data, req->len, req->flags);
    if (ret < 0) {
        printf("[ERR] NP: Failed to send on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Data sent successfully (real_fd=%d, bytes=%d)\r\n",
               entry->real_fd, ret);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket sendto RPC
 */
static int handle_socket_sendto(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_sendto] + addr + data */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_sendto *req = (struct qc7xx_rpc_socket_sendto *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char *send_data;
    struct sockaddr *addr_ptr;
    int ret;

    /* Address and data follow the header struct (AP sends them after the struct) */
    addr_ptr = (struct sockaddr *)((uint8_t *)data + 4 + sizeof(struct qc7xx_rpc_socket_sendto));
    send_data = (char *)((uint8_t *)addr_ptr + req->addrlen);

    if (addr_ptr && req->addrlen > 0 && addr_ptr->sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)addr_ptr;
        char ip_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
        printf("[INF] NP: Target Address: %s:%d (IPv4)\r\n", ip_str, ntohs(addr_in->sin_port));
    }
    if (send_data && req->len > 0) {
        printf("[INF] NP: Data: %.*s\r\n", (int)MIN(req->len, 64), send_data);
    }

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

   if (addr_ptr && req->addrlen >= sizeof(struct sockaddr_in) && addr_ptr->sa_family == AF_INET) {
        struct sockaddr_in *addr_in = (struct sockaddr_in *)addr_ptr;
        char ip_str[NET_IPV4_ADDR_LEN];
        net_addr_ntop(AF_INET, &addr_in->sin_addr, ip_str, sizeof(ip_str));
        printf("[DBG] NP:   sendto_addr: %s:%d\r\n", ip_str, ntohs(addr_in->sin_port));
        printf("[DBG] NP:   sendto_addr_raw: 0x%02X%02X%02X%02X\r\n",
               addr_in->sin_addr.s_addr & 0xFF,
               (addr_in->sin_addr.s_addr >> 8) & 0xFF,
               (addr_in->sin_addr.s_addr >> 16) & 0xFF,
               (addr_in->sin_addr.s_addr >> 24) & 0xFF);
    }
    printf("[DBG] NP:   errno_before: %d\r\n", errno);
#if 0
    /* Check socket state before sendto */
    if (!entry->is_bound) {
        printf("[WRN] NP: Socket is not bound before sendto!\r\n");
    }
#endif
    if (addr_ptr && req->addrlen > 0 && addr_ptr->sa_family == AF_INET) {
        printf("use sendto api\r\n");    
        ret = zsock_sendto(entry->real_fd, send_data, req->len, req->flags,
                      addr_ptr, req->addrlen);
    } else {
        printf("use send api\r\n");    
        ret = zsock_send(entry->real_fd, send_data, req->len, req->flags);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket listen RPC
 */
static int handle_socket_listen(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_listen] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_listen *req = (struct qc7xx_rpc_socket_listen *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    int ret;

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket listen failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    if (!entry->is_bound) {
        printf("[ERR] NP: Socket listen failed - socket not bound (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EINVAL, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call listen on the real socket */
    ret = zsock_listen(entry->real_fd, req->backlog);
    if (ret < 0) {
        printf("[ERR] NP: Failed to listen on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Socket listening successfully (fd=%d, real_fd=%d, backlog=%d)\r\n",
               req->sockfd, entry->real_fd, req->backlog);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket accept RPC
 */
static int handle_socket_accept(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_accept] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_accept *req = (struct qc7xx_rpc_socket_accept *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry, *new_entry;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int real_new_fd, client_new_fd;
    int ret;

    printf("[DBG] NP: Received socket accept RPC (fd=%d)\r\n", req->sockfd);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket accept failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call accept on the real socket (blocking) */
    real_new_fd = zsock_accept(entry->real_fd, (struct sockaddr *)&addr, &addr_len);
    if (real_new_fd < 0) {
        printf("[ERR] NP: Failed to accept connection on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
        k_mutex_unlock(&entry->lock);
        return send_rpc_response(rpcs, rpc_id, -1, errno, NULL, 0);
    }

    /* Allocate a new client FD for the accepted connection */
    client_new_fd = alloc_socket_fd(entry->family, entry->type, entry->protocol);
    if (client_new_fd < 0) {
        printf("[ERR] NP: Failed to allocate client FD for accepted connection\r\n");
        zsock_close(real_new_fd);
        k_mutex_unlock(&entry->lock);
        return send_rpc_response(rpcs, rpc_id, -1, EMFILE, NULL, 0);
    }

    /* Map the new client FD to the real FD */
    new_entry = find_socket_entry(client_new_fd);
    if (new_entry) {
        new_entry->real_fd = real_new_fd;
        new_entry->is_connected = true;
        memcpy(&new_entry->remote_addr, &addr, addr_len);
    }

    printf("[DBG] NP: Connection accepted (client_fd=%d, real_fd=%d)\r\n",
           client_new_fd, real_new_fd);

    k_mutex_unlock(&entry->lock);

    /* Send response with the new client FD */
    ret = send_rpc_response(rpcs, rpc_id, client_new_fd, 0, NULL, 0);
    printf("[DBG] NP: Sent socket accept response (client_fd=%d, ret=%d)\r\n",
           client_new_fd, ret);

    return ret;
}

/**
 * @brief Handle socket recv RPC
 */
static int handle_socket_recv(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_recv] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_recv *req = (struct qc7xx_rpc_socket_recv *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char recv_buffer[RPC_BUFFER_SIZE];
    int ret;

    printf("[DBG] NP: Received socket recv RPC (fd=%d, len=%zu)\r\n", req->sockfd, req->len);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket recv failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    if (!entry->is_connected) {
        printf("[ERR] NP: Socket recv failed - not connected (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, ENOTCONN, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call recv on the real socket */
    size_t recv_len = MIN(req->len, sizeof(recv_buffer));
    ret = zsock_recv(entry->real_fd, recv_buffer, recv_len, req->flags);
    if (ret < 0) {
        printf("[ERR] NP: Failed to recv from real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Received %d bytes from real socket (real_fd=%d)\r\n",
               ret, entry->real_fd);
    }

    k_mutex_unlock(&entry->lock);

    /* Send response with received data */
    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0,
                           ret > 0 ? recv_buffer : NULL, ret > 0 ? ret : 0);
}

/**
 * @brief Handle socket recvfrom RPC
 */
static int handle_socket_recvfrom(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_recvfrom] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_recvfrom *req = (struct qc7xx_rpc_socket_recvfrom *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char recv_buffer[RPC_BUFFER_SIZE];
    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);
    int ret;

    printf("[DBG] NP: Received socket recvfrom RPC (fd=%d, len=%zu)\r\n", req->sockfd, req->len);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket recvfrom failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call recvfrom on the real socket */
    size_t recv_len = MIN(req->len, sizeof(recv_buffer));
    ret = zsock_recvfrom(entry->real_fd, recv_buffer, recv_len, req->flags,
                        (struct sockaddr *)&src_addr, &src_addr_len);
    if (ret < 0) {
        printf("[ERR] NP: Failed to recvfrom from real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Received %d bytes via recvfrom (real_fd=%d)\r\n",
               ret, entry->real_fd);
    }

    k_mutex_unlock(&entry->lock);

    /* Send response with received data and source address */
    struct rpc_response response;
    response.result = ret;
    response.errno_val = ret < 0 ? errno : 0;
    response.data_len = 0;

    if (ret > 0) {
        size_t copy_len = MIN(ret, sizeof(response.data));
        memcpy(response.data, recv_buffer, copy_len);
        response.data_len = copy_len;

        /* Append source address after the data */
        if (src_addr_len <= sizeof(response.data) - response.data_len) {
            memcpy(response.data + response.data_len, &src_addr, src_addr_len);
            response.data_len += src_addr_len;
        }
    }

    return send_rpc_response(rpcs, rpc_id, response.result, response.errno_val,
                           response.data_len > 0 ? response.data : NULL,
                           response.data_len);
}

/**
 * @brief Handle socket setsockopt RPC
 */
static int handle_socket_setsockopt(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_setsockopt] + optval */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_setsockopt *req = (struct qc7xx_rpc_socket_setsockopt *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char *optval;
    int ret;

    printf("[DBG] NP: Received socket setsockopt RPC (fd=%d, level=%d, optname=%d, optlen=%d)\r\n",
           req->sockfd, req->level, req->optname, req->optlen);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    /* Option value follows the header struct */
    optval = (char *)((uint8_t *)data + 4 + sizeof(struct qc7xx_rpc_socket_setsockopt));

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket setsockopt failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call setsockopt on the real socket */
    ret = zsock_setsockopt(entry->real_fd, req->level, req->optname, optval, req->optlen);
    if (ret < 0) {
        printf("[ERR] NP: Failed to setsockopt on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: setsockopt completed successfully (real_fd=%d)\r\n", entry->real_fd);
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/**
 * @brief Handle socket getsockopt RPC
 */
static int handle_socket_getsockopt(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [struct qc7xx_rpc_socket_getsockopt] */
    uint32_t rpc_id = *(uint32_t *)data;
    struct qc7xx_rpc_socket_getsockopt *req = (struct qc7xx_rpc_socket_getsockopt *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    char optval_buffer[256];
    socklen_t optlen = req->optlen;
    int ret;

    printf("[DBG] NP: Received socket getsockopt RPC (fd=%d, level=%d, optname=%d, optlen=%d)\r\n",
           req->sockfd, req->level, req->optname, req->optlen);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(req->sockfd);
    if (!entry) {
        printf("[ERR] NP: Socket getsockopt failed - fd not found (fd=%d)\r\n", req->sockfd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    if (optlen > sizeof(optval_buffer)) {
        optlen = sizeof(optval_buffer);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call getsockopt on the real socket */
    ret = zsock_getsockopt(entry->real_fd, req->level, req->optname, optval_buffer, &optlen);
    if (ret < 0) {
        printf("[ERR] NP: Failed to getsockopt on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: getsockopt completed successfully (real_fd=%d, optlen=%d)\r\n",
               entry->real_fd, optlen);
    }

    k_mutex_unlock(&entry->lock);

    /* Send response with option value */
    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0,
                           ret == 0 ? optval_buffer : NULL, ret == 0 ? optlen : 0);
}

/**
 * @brief Handle socket getsockname RPC
 */
static int handle_socket_getsockname(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [int sockfd] */
    uint32_t rpc_id = *(uint32_t *)data;
    int fd = *(int *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int ret;

    printf("[DBG] NP: Received socket getsockname RPC (fd=%d)\r\n", fd);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(fd);
    if (!entry) {
        printf("[ERR] NP: Socket getsockname failed - fd not found (fd=%d)\r\n", fd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call getsockname on the real socket */
    ret = zsock_getsockname(entry->real_fd, (struct sockaddr *)&addr, &addr_len);
    if (ret < 0) {
        printf("[ERR] NP: Failed to getsockname on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: getsockname completed successfully (real_fd=%d, addr_len=%d)\r\n",
               entry->real_fd, addr_len);
    }

    k_mutex_unlock(&entry->lock);

    /* Send response with socket address */
    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0,
                           ret == 0 ? &addr : NULL, ret == 0 ? addr_len : 0);
}

/**
 * @brief Handle socket getpeername RPC
 */
static int handle_socket_getpeername(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [int sockfd] */
    uint32_t rpc_id = *(uint32_t *)data;
    int fd = *(int *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    struct socket_entry *entry;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    int ret;

    printf("[DBG] NP: Received socket getpeername RPC (fd=%d)\r\n", fd);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(fd);
    if (!entry) {
        printf("[ERR] NP: Socket getpeername failed - fd not found (fd=%d)\r\n", fd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    if (!entry->is_connected) {
        printf("[ERR] NP: Socket getpeername failed - not connected (fd=%d)\r\n", fd);
        return send_rpc_response(rpcs, rpc_id, -1, ENOTCONN, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call getpeername on the real socket */
    ret = zsock_getpeername(entry->real_fd, (struct sockaddr *)&addr, &addr_len);
    if (ret < 0) {
        printf("[ERR] NP: Failed to getpeername on real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: getpeername completed successfully (real_fd=%d, addr_len=%d)\r\n",
               entry->real_fd, addr_len);
    }

    k_mutex_unlock(&entry->lock);

    /* Send response with peer address */
    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0,
                           ret == 0 ? &addr : NULL, ret == 0 ? addr_len : 0);
}

/**
 * @brief Handle socket shutdown RPC
 */
static int handle_socket_shutdown(void *data, struct rpmsg_rpc_svr *rpcs)
{
    /* Data format: [4 bytes RPC ID] + [int sockfd] + [int how] */
    uint32_t rpc_id = *(uint32_t *)data;
    int fd = *(int *)((uint8_t *)data + MAX_FUNC_ID_LEN);
    int how = *(int *)((uint8_t *)data + 8);  /* Extract how parameter */
    struct socket_entry *entry;
    int ret;

    printf("[DBG] NP: Received socket shutdown RPC (fd=%d, how=%d)\r\n", fd, how);
    printf("[DBG] NP: RPC ID: %u\r\n", rpc_id);

    entry = find_socket_entry(fd);
    if (!entry) {
        printf("[ERR] NP: Socket shutdown failed - fd not found (fd=%d)\r\n", fd);
        return send_rpc_response(rpcs, rpc_id, -1, EBADF, NULL, 0);
    }

    k_mutex_lock(&entry->lock, K_FOREVER);

    /* Call shutdown on the real socket */
    ret = zsock_shutdown(entry->real_fd, how);
    if (ret < 0) {
        printf("[ERR] NP: Failed to shutdown real socket (real_fd=%d): errno=%d\r\n",
               entry->real_fd, errno);
    } else {
        printf("[DBG] NP: Socket shutdown successfully (real_fd=%d)\r\n", entry->real_fd);
        entry->is_connected = false;
    }

    k_mutex_unlock(&entry->lock);

    return send_rpc_response(rpcs, rpc_id, ret, ret < 0 ? errno : 0, NULL, 0);
}

/* RPC service table - Use the same IDs as AP core */
static const struct rpmsg_rpc_services socket_rpc_services[] = {
    { .id = QC7XX_RPC_SOCKET_CREATE, .cb_function = handle_socket_create },
    { .id = QC7XX_RPC_SOCKET_CLOSE, .cb_function = handle_socket_close },
    { .id = QC7XX_RPC_SOCKET_BIND, .cb_function = handle_socket_bind },
    { .id = QC7XX_RPC_SOCKET_CONNECT, .cb_function = handle_socket_connect },
    { .id = QC7XX_RPC_SOCKET_LISTEN, .cb_function = handle_socket_listen },
    { .id = QC7XX_RPC_SOCKET_ACCEPT, .cb_function = handle_socket_accept },
    { .id = QC7XX_RPC_SOCKET_SEND, .cb_function = handle_socket_send },
    { .id = QC7XX_RPC_SOCKET_SENDTO, .cb_function = handle_socket_sendto },
    { .id = QC7XX_RPC_SOCKET_RECV, .cb_function = handle_socket_recv },
    { .id = QC7XX_RPC_SOCKET_RECVFROM, .cb_function = handle_socket_recvfrom },
    { .id = QC7XX_RPC_SOCKET_SETSOCKOPT, .cb_function = handle_socket_setsockopt },
    { .id = QC7XX_RPC_SOCKET_GETSOCKOPT, .cb_function = handle_socket_getsockopt },
    { .id = QC7XX_RPC_SOCKET_GETSOCKNAME, .cb_function = handle_socket_getsockname },
    { .id = QC7XX_RPC_SOCKET_GETPEERNAME, .cb_function = handle_socket_getpeername },
    { .id = QC7XX_RPC_SOCKET_SHUTDOWN, .cb_function = handle_socket_shutdown },
};

/**
 * @brief Initialize RPC server
 */
static int init_rpc_server(struct rpmsg_device *rdev)
{
    int ret;

    printf("[DBG] NP: Initializing RPC server\r\n");

    /* Initialize socket table */
    memset(&g_rpc_server, 0, sizeof(g_rpc_server));
    g_rpc_server.next_fd = SOCKET_FD_BASE;
    k_mutex_init(&g_rpc_server.socket_table_mutex);

    printf("[DBG] NP: Initializing rpmsg_rpc_server with %d services\r\n",
           ARRAY_SIZE(socket_rpc_services));

    /* Initialize RPC server */
    ret = rpmsg_rpc_server_init(&g_rpc_server.rpc_server, rdev,
                               socket_rpc_services,
                               ARRAY_SIZE(socket_rpc_services),
                               NULL);
    if (ret < 0) {
        printf("[ERR] NP: Failed to initialize RPC server (ret=%d)\r\n", ret);
        return ret;
    }

    g_rpc_server.initialized = true;
    printf("[DBG] NP: RPC server initialized successfully\r\n");

    return 0;
}


/**
 * @brief Initialize RPC service for socket offload
 */
int rpc_service_init(void)
{
    int ret;
    struct rpmsg_device *rdev;

    printf("[DBG] NP: Initializing RPC service\r\n");

    /* Create RPMsg device - platform specific */
    extern struct rpmsg_device *platform_create_rpmsg_vdev(void);
    rdev = platform_create_rpmsg_vdev();
    if (!rdev) {
        printf("[ERR] NP: Failed to create RPMsg device\r\n");
        return -ENODEV;
    }

    printf("[DBG] NP: RPMsg device created successfully\r\n");

    /* Initialize RPC server directly */
    ret = init_rpc_server(rdev);
    if (ret < 0) {
        return ret;
    }

    printf("[DBG] NP: RPC service initialization completed\r\n");
    return 0;
}

/* Shell commands for debugging RPC service */
#ifdef CONFIG_SHELL

static int cmd_rpc_status(const struct shell *sh, size_t argc, char **argv)
{
    int active_sockets = 0;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Network Core RPC Service Status:");
    shell_print(sh, "  RPC Server: %s",
                g_rpc_server.initialized ? "Active" : "Inactive");

    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (g_rpc_server.sockets[i].is_used) {
            active_sockets++;
        }
    }

    shell_print(sh, "  Active Sockets: %d/%d", active_sockets, MAX_SOCKETS);

    return 0;
}

static int cmd_rpc_list_sockets(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Active Sockets:");
    shell_print(sh, "  FD    Type    State");
    shell_print(sh, "  --    ----    -----");

    for (int i = 0; i < MAX_SOCKETS; i++) {
        struct socket_entry *entry = &g_rpc_server.sockets[i];
        if (entry->is_used) {
            const char *type_str = (entry->type == SOCK_STREAM) ? "TCP" : "UDP";
            const char *state_str = entry->is_connected ? "CONNECTED" :
                                  (entry->is_bound ? "BOUND" : "CREATED");

            shell_print(sh, "  %-4d %-7s %s", entry->fd, type_str, state_str);
        }
    }

    return 0;
}

/* Shell commands temporarily disabled
SHELL_CMD_REGISTER(rpc_status, NULL, "Show RPC service status", cmd_rpc_status);
SHELL_CMD_REGISTER(rpc_list_sockets, NULL, "List active sockets", cmd_rpc_list_sockets);
*/

#endif /* CONFIG_SHELL */

/**
 * @brief Shutdown RPC service
 */
void rpc_service_shutdown(void)
{
    if (g_rpc_server.initialized) {
        printf("[DBG] NP: Shutting down RPC service\r\n");
        /* TODO: Add cleanup code for RPC server */
        g_rpc_server.initialized = false;
        printf("[DBG] NP: RPC service shutdown completed\r\n");
    }
}

/**
 * @brief Check if RPC service is initialized
 */
bool rpc_service_is_initialized(void)
{
    return g_rpc_server.initialized;
}
