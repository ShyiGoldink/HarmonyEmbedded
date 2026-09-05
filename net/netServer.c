#include "netServer.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ohos_init.h"
#include "cmsis_os2.h"
#include "wifi_device.h"
#include "lwip/sockets.h"
#include "lwip/netifapi.h"

// ===== 静态变量（相当于私有成员） =====
static int g_sock_fd = -1;           // Socket 文件描述符
static int g_is_connected = 0;       // 连接状态

// ===== 内部函数：连接 WiFi =====
static int ConnectWiFi(const char* ssid, const char* password) {
    WifiDeviceConfig config = {0};
    strncpy(config.ssid, ssid, sizeof(config.ssid) - 1);
    strncpy(config.preSharedKey, password, sizeof(config.preSharedKey) - 1);
    config.securityType = WIFI_SEC_TYPE_PSK;

    // 启用 WiFi
    if (EnableWifi() != WIFI_SUCCESS) {
        printf("EnableWifi 失败\n");
        return -1;
    }

    // 添加设备配置
    if (AddDeviceConfig(&config, NULL) != WIFI_SUCCESS) {
        printf("AddDeviceConfig 失败\n");
        return -1;
    }

    // 连接 WiFi
    if (ConnectTo(ssid) != WIFI_SUCCESS) {
        printf("ConnectTo 失败\n");
        return -1;
    }

    // 等待 IP 分配
    osDelay(3000);
    printf("WiFi 连接成功\n");
    return 0;
}

// ===== 内部函数：连接服务器 =====
static int ConnectServer(const char* server_ip, int server_port) {
    // 创建 Socket
    g_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock_fd < 0) {
        printf("socket 创建失败\n");
        return -1;
    }

    // 配置服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = inet_addr(server_ip);

    // 连接服务器
    if (connect(g_sock_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("connect 服务器失败\n");
        close(g_sock_fd);
        g_sock_fd = -1;
        return -1;
    }

    printf("服务器连接成功: %s:%d\n", server_ip, server_port);
    return 0;
}

// ===== 对外接口实现 =====

int Net_Init(const char* ssid, const char* password, const char* server_ip, int server_port) {
    // 1. 连接 WiFi
    if (ConnectWiFi(ssid, password) != 0) {
        return -1;
    }

    // 2. 连接服务器
    if (ConnectServer(server_ip, server_port) != 0) {
        return -1;
    }

    g_is_connected = 1;
    return 0;
}

int Net_Send(const char* data, size_t len) {
    if (!g_is_connected || g_sock_fd < 0) {
        return -1;
    }
    return send(g_sock_fd, data, len, 0);
}

int Net_Recv(char* buffer, size_t buf_size) {
    if (!g_is_connected || g_sock_fd < 0) {
        return -1;
    }
    return recv(g_sock_fd, buffer, buf_size, 0);
}

void Net_Close(void) {
    if (g_sock_fd >= 0) {
        close(g_sock_fd);
        g_sock_fd = -1;
    }
    g_is_connected = 0;
    printf("网络已关闭\n");
}

int Net_IsConnected(void) {
    return g_is_connected;
}