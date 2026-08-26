/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QC7XX_IPC_SOCKET_H_
#define QC7XX_IPC_SOCKET_H_

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include <openamp/rpmsg_rpc_client_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief QC7xx Socket RPC Service IDs
 * Using simple sequential IDs to match NP core expectations
 */
#define QC7XX_RPC_SOCKET_CREATE        1
#define QC7XX_RPC_SOCKET_CLOSE         2
#define QC7XX_RPC_SOCKET_BIND          3
#define QC7XX_RPC_SOCKET_CONNECT       4
#define QC7XX_RPC_SOCKET_LISTEN        5
#define QC7XX_RPC_SOCKET_ACCEPT        6
#define QC7XX_RPC_SOCKET_SEND          7
#define QC7XX_RPC_SOCKET_SENDTO        8
#define QC7XX_RPC_SOCKET_RECV          9
#define QC7XX_RPC_SOCKET_RECVFROM      10
#define QC7XX_RPC_SOCKET_SETSOCKOPT    11
#define QC7XX_RPC_SOCKET_GETSOCKOPT    12
#define QC7XX_RPC_SOCKET_GETSOCKNAME   13
#define QC7XX_RPC_SOCKET_GETPEERNAME   14
#define QC7XX_RPC_SOCKET_SHUTDOWN      15

/* Maximum RPC buffer size (from RPMsg RPC) */
#define QC7XX_RPC_MAX_BUF_LEN          1536UL

/**
 * @brief RPC response callback context
 */
struct qc7xx_rpc_ctx {
    int result;                /* Result value */
    int errno_val;             /* errno value if result < 0 */
    socklen_t addrlen;        /* Actual address length */
    socklen_t optlen;         /* Actual option length */
    size_t data_len;          /* Length of returned data */
    uint8_t data[QC7XX_RPC_MAX_BUF_LEN]; /* Response data */
    struct k_sem sem;          /* Semaphore for synchronization */
    bool completed;            /* Completion flag */
};

struct rpc_response {
    int result;                /* Result value */
    int errno_val;             /* errno value if result < 0 */
    size_t data_len;          /* Length of returned data */
    uint8_t data[QC7XX_RPC_MAX_BUF_LEN]; /* Response data */
};

/**
 * @brief Socket create parameters
 */
struct qc7xx_rpc_socket_create {
    int family;
    int type;
    int proto;
};

/**
 * @brief Socket bind parameters
 */
struct qc7xx_rpc_socket_bind {
    int sockfd;
    socklen_t addrlen;
    struct sockaddr addr;    /* Flexible array - actual size determined by addrlen */
};

/**
 * @brief Socket connect parameters
 */
struct qc7xx_rpc_socket_connect {
    int sockfd;
    socklen_t addrlen;
    struct sockaddr addr;    /* Flexible array - actual size determined by addrlen */
};

/**
 * @brief Socket listen parameters
 */
struct qc7xx_rpc_socket_listen {
    int sockfd;
    int backlog;
};

/**
 * @brief Socket accept parameters
 */
struct qc7xx_rpc_socket_accept {
    int sockfd;
    socklen_t addrlen;
    /* Response: int new_sockfd + struct sockaddr */
};

/**
 * @brief Socket send parameters
 */
struct qc7xx_rpc_socket_send {
    int sockfd;
    int flags;
    size_t len;
    char data[];              /* Flexible array for data */
};

/**
 * @brief Socket sendto parameters
 */
struct qc7xx_rpc_socket_sendto {
    int sockfd;
    int flags;
    size_t len;
    socklen_t addrlen;
    struct sockaddr addr;    /* Destination address */
    char data[];              /* Flexible array for data */
};

/**
 * @brief Socket recv parameters
 */
struct qc7xx_rpc_socket_recv {
    int sockfd;
    int flags;
    size_t len;
};

/**
 * @brief Socket recvfrom parameters
 */
struct qc7xx_rpc_socket_recvfrom {
    int sockfd;
    int flags;
    size_t len;
    socklen_t addrlen;
};

/**
 * @brief Socket setsockopt parameters
 */
struct qc7xx_rpc_socket_setsockopt {
    int sockfd;
    int level;
    int optname;
    socklen_t optlen;
    char data[];              /* Flexible array for option value */
};

/**
 * @brief Socket getsockopt parameters
 */
struct qc7xx_rpc_socket_getsockopt {
    int sockfd;
    int level;
    int optname;
    socklen_t optlen;
};

/**
 * @brief Socket getsockname parameters
 */
struct qc7xx_rpc_socket_getsockname {
    int sockfd;
    socklen_t addrlen;
};

/**
 * @brief Socket getpeername parameters
 */
struct qc7xx_rpc_socket_getpeername {
    int sockfd;
    socklen_t addrlen;
};

/**
 * @brief Initialize IPC socket communication using RPMsg RPC
 *
 * This function initializes the RPMsg RPC client for socket operations.
 * It creates an RPC client and registers it with the RPMsg service.
 *
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_init(void);

/**
 * @brief Check if RPC client is initialized
 *
 * @return true if initialized, false otherwise
 */
bool qc7xx_ipc_socket_is_initialized(void);

/**
 * @brief Initialize RPMsg and RPC for socket offload
 *
 * @return 0 on success, negative errno on failure
 */
int qc7xx_rpmsg_and_rpc_init(void);

/**
 * @brief Shutdown RPMsg and RPC
 */
void qc7xx_rpmsg_and_rpc_shutdown(void);

/**
 * @brief Check if RPMsg is initialized
 *
 * @return true if initialized, false otherwise
 */
bool qc7xx_rpmsg_is_initialized(void);

/**
 * @brief Get RPMsg device instance
 *
 * @return RPMsg device pointer or NULL if not initialized
 */
struct rpmsg_device *qc7xx_get_rpmsg_device(void);

/**
 * @brief Shutdown RPMsg RPC client
 */
void qc7xx_rpc_client_shutdown(void);

/**
 * @brief RPC response callback function
 *
 * This function is called when the NP core responds to an RPC request.
 *
 * @param rpc RPC client instance
 * @param status RPC status
 * @param data Response data
 * @param len Response data length
 */
void qc7xx_rpc_callback(struct rpmsg_rpc_clt *rpc, int status, void *data, size_t len);

/**
 * @brief Create a socket via IPC to NP core
 *
 * @param family Address family
 * @param type Socket type
 * @param proto Protocol
 * @return Socket file descriptor on success, negative errno on failure
 */
int qc7xx_ipc_socket_create(int family, int type, int proto);

/**
 * @brief Close a socket via IPC to NP core
 *
 * @param sockfd Socket file descriptor
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_close(int sockfd);

/**
 * @brief Bind a socket to an address via IPC
 *
 * @param sockfd Socket file descriptor
 * @param addr Address to bind to
 * @param addrlen Address length
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Connect a socket to an address via IPC
 *
 * @param sockfd Socket file descriptor
 * @param addr Address to connect to
 * @param addrlen Address length
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief Listen for connections on a socket via IPC
 *
 * @param sockfd Socket file descriptor
 * @param backlog Maximum pending connections
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_listen(int sockfd, int backlog);

/**
 * @brief Accept a connection on a listening socket via IPC
 *
 * @param sockfd Socket file descriptor
 * @param addr Address of connecting peer (can be NULL)
 * @param addrlen Length of address (input/output)
 * @return New socket file descriptor on success, negative errno on failure
 */
int qc7xx_ipc_socket_accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Send data to a socket via IPC
 *
 * @param sockfd Socket file descriptor
 * @param buf Buffer containing data to send
 * @param len Number of bytes to send
 * @param flags Flags (MSG_*)
 * @return Number of bytes sent on success, negative errno on failure
 */
ssize_t qc7xx_ipc_socket_send(int sockfd, const void *buf, size_t len, int flags);

/**
 * @brief Send data to a specific address via IPC
 *
 * @param sockfd Socket file descriptor
 * @param buf Buffer containing data to send
 * @param len Number of bytes to send
 * @param flags Flags (MSG_*)
 * @param dest_addr Destination address (for connectionless sockets)
 * @param addrlen Length of destination address
 * @return Number of bytes sent on success, negative errno on failure
 */
ssize_t qc7xx_ipc_socket_sendto(int sockfd, const void *buf, size_t len, int flags,
                                const struct sockaddr *dest_addr, socklen_t addrlen);

/**
 * @brief Receive data from a socket via IPC
 *
 * @param sockfd Socket file descriptor
 * @param buf Buffer to store received data
 * @param len Size of buffer
 * @param flags Flags (MSG_*)
 * @return Number of bytes received on success, negative errno on failure
 */
ssize_t qc7xx_ipc_socket_recv(int sockfd, void *buf, size_t len, int flags);

/**
 * @brief Receive data from a socket via IPC with source address
 *
 * @param sockfd Socket file descriptor
 * @param buf Buffer to store received data
 * @param len Size of buffer
 * @param flags Flags (MSG_*)
 * @param src_addr Source address (can be NULL)
 * @param addrlen Length of source address (input/output)
 * @return Number of bytes received on success, negative errno on failure
 */
ssize_t qc7xx_ipc_socket_recvfrom(int sockfd, void *buf, size_t len, int flags,
                                  struct sockaddr *src_addr, socklen_t *addrlen);

/**
 * @brief Set socket options via IPC
 *
 * @param sockfd Socket file descriptor
 * @param level Option level
 * @param optname Option name
 * @param optval Option value
 * @param optlen Option length
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_setsockopt(int sockfd, int level, int optname,
                               const void *optval, socklen_t optlen);

/**
 * @brief Get socket options via IPC
 *
 * @param sockfd Socket file descriptor
 * @param level Option level
 * @param optname Option name
 * @param optval Buffer to store option value
 * @param optlen Option length (input/output)
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_getsockopt(int sockfd, int level, int optname,
                               void *optval, socklen_t *optlen);

/**
 * @brief Get socket name via IPC
 *
 * @param sockfd Socket file descriptor
 * @param addr Buffer to store socket address
 * @param addrlen Address length (input/output)
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_getsockname(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Get peer name via IPC
 *
 * @param sockfd Socket file descriptor
 * @param addr Buffer to store peer address
 * @param addrlen Address length (input/output)
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_getpeername(int sockfd, struct sockaddr *addr, socklen_t *addrlen);

/**
 * @brief Shutdown socket via IPC
 *
 * @param sockfd Socket file descriptor
 * @param how How to shutdown (SHUT_RD, SHUT_WR, SHUT_RDWR)
 * @return 0 on success, negative errno on failure
 */
int qc7xx_ipc_socket_shutdown(int sockfd, int how);

#ifdef __cplusplus
}
#endif

#endif /* QC7XX_IPC_SOCKET_H_ */
