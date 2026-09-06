#ifndef NET_SERVICE_H
#define NET_SERVICE_H

#include <stddef.h>

// ---------- 初始化 ----------
// 功能：连接 WiFi + 创建 Socket + 连接服务器
// 参数：ssid=WiFi名称, password=WiFi密码, server_ip=服务器IP, server_port=端口
// 返回：0=成功, -1=失败
int Net_Init(const char* ssid, const char* password, const char* server_ip, int server_port);

// ---------- 发送数据 ----------
// 参数：data=要发送的数据, len=数据长度
// 返回：实际发送的字节数, -1=失败
int Net_Send(const char* data, size_t len);

// ---------- 接收数据 ----------
// 参数：buffer=接收缓冲区, buf_size=缓冲区大小
// 返回：实际接收的字节数, -1=失败, 0=连接关闭
int Net_Recv(char* buffer, size_t buf_size);

// ---------- 关闭连接 ----------
void Net_Close(void);

// ---------- 接收超时 ----------
// 设置 recv 阻塞超时（秒），0 表示一直阻塞
int Net_SetRecvTimeout(int seconds);

// ---------- 状态查询 ----------
int Net_IsConnected(void);

#endif