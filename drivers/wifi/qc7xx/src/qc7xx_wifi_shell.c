/*
 * Copyright (c) 2026 Qualcomm Technologies, Inc. and/or its subsidiaries
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <wireless_api.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_ip.h>
#include "qcc74x_glb.h"

static int soc_wifi_info_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
	uint32_t tot, used;

	extern void net_al_tx_buf_get(uint32_t *tot, uint32_t *used);
	extern int fhost_rxbufcnt_get(uint32_t *tot, uint32_t *used);

	printf("wireless version :%s\r\n", WIRELESS_VERSION);

	net_al_tx_buf_get(&tot, &used);
	printf("fhost tx buf used:%d, tot:%d\r\n", used, tot);
	fhost_rxbufcnt_get(&tot, &used);
	printf("fhost rx buf used:%d, tot:%d\r\n", used, tot);

	printf("HW rx rb start:0x%08X, end:0x%08X, r:0x%08X, w:0x%08X\r\n",
            *(volatile uint32_t *)0x24b081c8, *(volatile uint32_t *)0x24b081cc,
            *(volatile uint32_t *)0x24b081d0, *(volatile uint32_t *)0x24b081d4);

    return 0;
}

static int soc_wifi_test_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
#if 0
	extern volatile uint32_t g_sta2ap_flag;

	if (g_sta2ap_flag) {
		g_sta2ap_flag = 0;
	} else {
		g_sta2ap_flag = 1;
	}
	printf("g_sta2ap_flag:%d\r\n", g_sta2ap_flag);
#endif
	return 0;
}



/* UDP Server Configuration */
#define UDP_SERVER_PORT       11111
#define UDP_SERVER_STACK_SIZE 4096
#define UDP_SERVER_PRIORITY   2

static K_THREAD_STACK_DEFINE(udp_server_stack, UDP_SERVER_STACK_SIZE);
static struct k_thread udp_server_thread;

static void udp_server_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int sock;
    struct sockaddr_in addr;
    char recv_buf[128];
    int received;

    // Create UDP socket
    sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        printf("Failed to create UDP socket: %d\r\n", errno);
        return;
    }

    // Configure server address
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;              // IPv4 address family
    addr.sin_port = htons(UDP_SERVER_PORT);  // Port number
    addr.sin_addr.s_addr = INADDR_ANY;       // Accept any incoming address

    // Bind socket to the server address
    if (zsock_bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        printf("Failed to bind UDP socket: %d\r\n", errno);
        goto cleanup;
    }

    printf("UDP server listening on port %d\r\n", UDP_SERVER_PORT);

    while (1) {
        // Receive data from clients
        received = zsock_recv(sock, recv_buf, sizeof(recv_buf) - 1, 0);
        if (received < 0) {
            printf("UDP recv error: %d\r\n", errno);
            k_sleep(K_MSEC(100));
            continue;
        }

        // Null-terminate received data for safe printing
        recv_buf[received] = '\0';

        // Print received data
        printf("Received %d bytes: %s\r\n", received, recv_buf);
    }

cleanup:
    zsock_close(sock);  // Clean up socket on exit
}

static int soc_wifi_udpserver_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
    // Create thread for UDP server
    k_thread_create(&udp_server_thread,
                    udp_server_stack,
                    K_THREAD_STACK_SIZEOF(udp_server_stack),
                    udp_server_task,
                    NULL, NULL, NULL,
                    UDP_SERVER_PRIORITY,
                    0, K_NO_WAIT);

    printf("UDP server task started\r\n");
    return 0;
}

/* TCP Server Implementation - Same format as UDP */
#define TCP_SERVER_PORT       22222
#define TCP_SERVER_STACK_SIZE 4096
#define TCP_SERVER_PRIORITY   2

static K_THREAD_STACK_DEFINE(tcp_server_stack, TCP_SERVER_STACK_SIZE);
static struct k_thread tcp_server_thread;

static void tcp_server_task(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);
    char recv_buf[128];
    int received;

    // Create TCP socket
    server_sock = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_sock < 0) {
        printf("Failed to create TCP socket: %d\r\n", errno);
        return;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;              // IPv4 address family
    server_addr.sin_port = htons(TCP_SERVER_PORT); // Port number
    server_addr.sin_addr.s_addr = INADDR_ANY;      // Accept any incoming address

    // Set SO_REUSEADDR to allow quick port reuse
    int optval = 1;
    zsock_setsockopt(server_sock, ZSOCK_SOL_SOCKET, ZSOCK_SO_REUSEADDR,
                     &optval, sizeof(optval));

    // Bind socket to the server address
    if (zsock_bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("Failed to bind TCP socket: %d\r\n", errno);
        goto cleanup;
    }

    // Start listening for incoming connections
    if (zsock_listen(server_sock, 1) < 0) {
        printf("TCP listen failed: %d\r\n", errno);
        goto cleanup;
    }

    printf("TCP server listening on port %d\r\n", TCP_SERVER_PORT);

    while (1) {
        // Accept incoming connection
        client_sock = zsock_accept(server_sock, (struct sockaddr *)&client_addr,
                                   &client_addr_len);
        if (client_sock < 0) {
            printf("TCP accept failed: %d\r\n", errno);
            continue;
        }

        printf("TCP client connected\r\n");

        // Handle client communication
        while (1) {
            // Receive data from client
            received = zsock_recv(client_sock, recv_buf, sizeof(recv_buf) - 1, 0);
            if (received <= 0) {
                if (received < 0) {
                    printf("TCP recv error: %d\r\n", errno);
                }
                break;
            }

            // Null-terminate received data for safe printing
            recv_buf[received] = '\0';

            // Print received data
            printf("TCP received %d bytes: %s\r\n", received, recv_buf);
        }

        // Close client socket
        zsock_close(client_sock);
        printf("TCP client disconnected\r\n");
    }

cleanup:
    zsock_close(server_sock);  // Clean up server socket on exit
}

static int soc_wifi_tcpserver_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
    // Create thread for TCP server
    k_thread_create(&tcp_server_thread,
                    tcp_server_stack,
                    K_THREAD_STACK_SIZEOF(tcp_server_stack),
                    tcp_server_task,
                    NULL, NULL, NULL,
                    TCP_SERVER_PRIORITY,
                    0, K_NO_WAIT);

    printf("TCP server task started\r\n");
    return 0;
}

static int soc_wifi_reboot_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
    printf("cmd ctrl reboot\r\n");
    k_sleep(K_MSEC(100));
    GLB_SW_POR_Reset();

    return 0;
}

static int soc_wifi_init_shell_cmd(const struct shell *shell, size_t argc, char **argv)
{
    void cmd_wifi_main(void);
    cmd_wifi_main();

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    sub_config,
    SHELL_CMD_ARG(info, NULL, "version and Wi-Fi buffer info\n", soc_wifi_info_shell_cmd, 1, 1),
    SHELL_CMD_ARG(test, NULL, "test\n", soc_wifi_test_shell_cmd, 1, 0),
    SHELL_CMD_ARG(udp, NULL, "udp server echo\n", soc_wifi_udpserver_shell_cmd, 1, 0),
    SHELL_CMD_ARG(tcp, NULL, "tcp server echo\n", soc_wifi_tcpserver_shell_cmd, 1, 0),
    SHELL_CMD_ARG(reboot, NULL, "system reboot\n", soc_wifi_reboot_shell_cmd, 1, 0),
    SHELL_CMD_ARG(init, NULL, "wifi main start\n", soc_wifi_init_shell_cmd, 1, 0),
    SHELL_SUBCMD_SET_END
);
SHELL_CMD_ARG_REGISTER(soc_wifi, &sub_config, "Wi-Fi commands", NULL, 1, 1);
