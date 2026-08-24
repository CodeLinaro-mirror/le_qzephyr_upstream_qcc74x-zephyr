/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef QC7XX_SOCKET_OFFLOAD_H_
#define QC7XX_SOCKET_OFFLOAD_H_

#include <zephyr/net/net_if.h>
#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize QC7xx socket offload functionality
 *
 * This function initializes the socket offload infrastructure for QC7xx AP core,
 * setting up IPC communication channels and registering DNS offload handlers.
 *
 * @param iface Network interface to associate with socket offload
 * @return 0 on success, negative errno on failure
 */
int qc7xx_socket_offload_init(struct net_if *iface);

/**
 * @brief Create an offloaded socket
 *
 * This is the socket creation callback that will be registered with the
 * socket offload subsystem. It creates a socket by forwarding the request
 * to NP core via IPC.
 *
 * @param family Address family (AF_INET, AF_INET6, AF_UNSPEC)
 * @param type Socket type (SOCK_STREAM, SOCK_DGRAM)
 * @param proto Protocol (IPPROTO_TCP, IPPROTO_UDP, 0)
 * @return Socket file descriptor on success, negative errno on failure
 */
int qc7xx_socket_create(int family, int type, int proto);

/**
 * @brief Check if socket parameters are supported
 *
 * @param family Address family to check
 * @param type Socket type to check
 * @param proto Protocol to check
 * @return true if supported, false otherwise
 */
bool qc7xx_socket_is_supported(int family, int type, int proto);

#ifdef __cplusplus
}
#endif

#endif /* QC7XX_SOCKET_OFFLOAD_H_ */
