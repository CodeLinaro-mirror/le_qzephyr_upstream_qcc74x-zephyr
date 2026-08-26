/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _QC7XX_RPC_SERVICE_H
#define _QC7XX_RPC_SERVICE_H

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <openamp/open_amp.h>
#include <openamp/rpmsg_rpc_client_server.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize RPC service for socket offload
 *
 * This function initializes the RPMsg device and sets up the RPC server
 * to handle socket operations from the AP core.
 *
 * @return 0 on success, negative errno on failure
 */
int rpc_service_init(void);

/**
 * @brief Shutdown RPC service
 *
 * This function shuts down the RPC server and cleans up resources.
 */
void rpc_service_shutdown(void);

/**
 * @brief Check if RPC service is initialized
 *
 * @return true if initialized, false otherwise
 */
bool rpc_service_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* _QC7XX_RPC_SERVICE_H */
