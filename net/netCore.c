#include "netCore.h"
#include "netServer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cmsis_os2.h"
#include "../tool/netData.h"
#include "../tool/eventBus.h"
#include "lwip/errno.h"

// 服务端协议（TCP 长连接，文本行，\n 结尾）：
//   上传:      SEND_SENSE_DATA/temp,humidity\n
//   拉时间:    GET_TIME/\n                    -> SEND_TIME/<ms>\n
//   拉表格:    GET_DATA_MINUTE/\n 等          -> SEND_DATA_TABEL/[h1..h10][t1..t10][startMs]\n
// 表格行内固定 10 个湿度、10 个温度，起始时间戳为第一个点的时间(毫秒)。

#define NETCORE_TABLE_POINTS   10
#define NETCORE_LINE_BUF_SIZE  512
#define NETCORE_CMD_BUF_SIZE   96

#define NETCORE_RSP_TIME       "SEND_TIME/"
#define NETCORE_RSP_TABLE      "SEND_DATA_TABEL/"

#define NETCORE_STEP_MINUTE_MS (60LL * 1000LL)
#define NETCORE_STEP_HOUR_MS   (60LL * 60LL * 1000LL)
#define NETCORE_STEP_DAY_MS    (24LL * 60LL * 60LL * 1000LL)

// 一次请求-响应期间持锁，避免传感器任务与 OLED 拉数据任务并发读写同一个 socket。
static osMutexId_t g_coreMutex = NULL;

// 接收缓冲：TCP 可能粘包/半包，这里按行读取并保留多余字节。
static char g_rxBuf[NETCORE_LINE_BUF_SIZE];
static size_t g_rxPos = 0;
static size_t g_rxEnd = 0;

static float g_tempData[NETCORE_TABLE_POINTS];
static float g_humiData[NETCORE_TABLE_POINTS];
static long long g_timerData[NETCORE_TABLE_POINTS];
static netData g_netData;

static int CoreLock(void)
{
    if (g_coreMutex == NULL) {
        g_coreMutex = osMutexNew(NULL);
        if (g_coreMutex == NULL) {
            return -1;
        }
    }
    if (osMutexAcquire(g_coreMutex, osWaitForever) != osOK) {
        return -1;
    }
    return 0;
}

static void CoreUnlock(void)
{
    if (g_coreMutex != NULL) {
        (void)osMutexRelease(g_coreMutex);
    }
}

static int NetCore_SendAll(const char* data)
{
    size_t total = strlen(data);
    size_t sent = 0;
    while (sent < total) {
        int n = Net_Send(data + sent, total - sent);
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

// 从接收缓冲读取一行（不含换行符）。粘在后面的字节会保留给下一次请求。
static int NetCore_RecvLine(char* line, size_t lineSize)
{
    size_t i;
    for (;;) {
        for (i = g_rxPos; i < g_rxEnd; i++) {
            if (g_rxBuf[i] == '\n') {
                size_t length = i - g_rxPos;
                if (length >= lineSize) {
                    length = lineSize - 1;
                }
                memcpy(line, g_rxBuf + g_rxPos, length);
                line[length] = '\0';
                if (length > 0 && line[length - 1] == '\r') {
                    line[length - 1] = '\0';
                }
                g_rxPos = i + 1;
                if (g_rxPos == g_rxEnd) {
                    g_rxPos = 0;
                    g_rxEnd = 0;
                }
                return 0;
            }
        }
        // 缓冲尾部已满：先压缩，仍满说明单行超过缓冲长度。
        if (g_rxEnd == NETCORE_LINE_BUF_SIZE) {
            if (g_rxPos > 0) {
                size_t unread = g_rxEnd - g_rxPos;
                memmove(g_rxBuf, g_rxBuf + g_rxPos, unread);
                g_rxPos = 0;
                g_rxEnd = unread;
            } else {
                return -1;
            }
        }
        if (g_rxEnd < NETCORE_LINE_BUF_SIZE) {
            int received = Net_Recv(g_rxBuf + g_rxEnd, NETCORE_LINE_BUF_SIZE - g_rxEnd);
        printf("[net] recv ret=%d errno=%d\n", received, errno);
        if (received <= 0) {
            return -1;
        }
        g_rxEnd += (size_t)received;
        }
    }
}

static int NetCore_ReadExpectedLine(char* line, size_t lineSize, const char* prefix, int maxTries)
{
    int tryCount;
    for (tryCount = 0; tryCount < maxTries; tryCount++) {
        if (NetCore_RecvLine(line, lineSize) != 0) {
            return -1;
        }
        if (strncmp(line, prefix, strlen(prefix)) == 0) {
            return 0;
        }
        printf("[net] skip unexpected line: %s\n", line);
    }
    return -1;
}

static void NetCore_PublishEmpty(EventType type)
{
    g_netData.time = 0;
    g_netData.count = 0;
    g_netData.tempData = NULL;
    g_netData.humiData = NULL;
    g_netData.timer = NULL;
    Event_Publish(type, &g_netData);
}

static void NetCore_PublishTime(long long timeMs)
{
    g_netData.time = timeMs;
    g_netData.count = 0;
    g_netData.tempData = NULL;
    g_netData.humiData = NULL;
    g_netData.timer = NULL;
    Event_Publish(EVENT_NET_TIME_READY, &g_netData);
}

static int NetCore_ParseFloatArray(const char** cursor, float* output)
{
    const char* p = *cursor;
    int i;
    if (*p != '[') {
        return -1;
    }
    p++;
    for (i = 0; i < NETCORE_TABLE_POINTS; i++) {
        char* end = NULL;
        output[i] = strtof(p, &end);
        if (end == p) {
            return -1;
        }
        p = end;
        while (*p == ' ' || *p == ',') {
            p++;
        }
    }
    if (*p != ']') {
        return -1;
    }
    *cursor = p + 1;
    return 0;
}

static int NetCore_ParseStartMs(const char** cursor, long long* startMs)
{
    const char* p = *cursor;
    char* end = NULL;
    if (*p != '[') {
        return -1;
    }
    p++;
    *startMs = strtoll(p, &end, 10);
    if (end == p) {
        return -1;
    }
    p = end;
    while (*p == ' ') {
        p++;
    }
    if (*p != ']') {
        return -1;
    }
    *cursor = p + 1;
    return 0;
}

static void NetCore_FillTableData(long long startMs, long long stepMs)
{
    int i;
    g_netData.time = startMs;
    g_netData.count = NETCORE_TABLE_POINTS;
    g_netData.tempData = g_tempData;
    g_netData.humiData = g_humiData;
    g_netData.timer = g_timerData;
    for (i = 0; i < NETCORE_TABLE_POINTS; i++) {
        g_timerData[i] = startMs + stepMs * i;
    }
}

static void NetCore_RequestTable(const char* request, long long stepMs)
{
    char line[NETCORE_LINE_BUF_SIZE];
    const char* payload = NULL;
    long long startMs = 0;
    int success = -1;

    if (CoreLock() != 0) {
        return;
    }
    if (NetCore_SendAll(request) != 0) {
            printf("[net] table send failed\n");
            goto finish;
    }
    printf("[net] table request sent\n");
    if (NetCore_ReadExpectedLine(line, sizeof(line), NETCORE_RSP_TABLE, 3) != 0) {
        goto finish;
    }
    payload = line + strlen(NETCORE_RSP_TABLE);
    if (NetCore_ParseFloatArray(&payload, g_humiData) != 0) {
        goto finish;
    }
    if (NetCore_ParseFloatArray(&payload, g_tempData) != 0) {
        goto finish;
    }
    if (NetCore_ParseStartMs(&payload, &startMs) != 0) {
        goto finish;
    }
    printf("[net] table rsp ok: %s\n", line);
    NetCore_FillTableData(startMs, stepMs);
    success = 0;

finish:
    if (success == 0) {
        Event_Publish(EVENT_NET_TABLE_READY, &g_netData);
    } else {
        // 简单版：失败也发空数据，方便 OLED 退出“等待刷新”状态。
        printf("[net] table request/parse failed\n");
        NetCore_PublishEmpty(EVENT_NET_TABLE_READY);
    }
    CoreUnlock();
}

// ================= 对外接口 =================

int Send_Sense_Data(float temp, float humidity)
{
    char command[NETCORE_CMD_BUF_SIZE];
    int length;
    int result = -1;
    if (CoreLock() != 0) {
        return -1;
    }
    length = snprintf(command, sizeof(command), "SEND_SENSE_DATA/%.2f,%.2f\n", temp, humidity);
    if (length > 0 && NetCore_SendAll(command) == 0) {
        result = 0;
    }
    CoreUnlock();
    return result;
}

long long getTime(void)
{
    char line[NETCORE_LINE_BUF_SIZE];
    long long timeMs = -1;
    int success = -1;
    if (CoreLock() != 0) {
        return -1;
    }
    if (NetCore_SendAll("GET_TIME/\n") == 0 &&
        NetCore_ReadExpectedLine(line, sizeof(line), NETCORE_RSP_TIME, 3) == 0) {
        timeMs = strtoll(line + strlen(NETCORE_RSP_TIME), NULL, 10);
        success = 0;
    }
    if (success == 0) {
        NetCore_PublishTime(timeMs);
    } else {
        NetCore_PublishEmpty(EVENT_NET_TIME_READY);
    }
    CoreUnlock();
    return timeMs;
}

void getDataMinute(void)
{
    printf("[net] getDataMinute begin\n");
    NetCore_RequestTable("GET_DATA_MINUTE/\n", NETCORE_STEP_MINUTE_MS);
}

void getDataHour(void)
{
    printf("[net] getDataHour begin\n");
    NetCore_RequestTable("GET_DATA_HOUR/\n", NETCORE_STEP_HOUR_MS);
}

void getDataDay(void)
{
    printf("[net] getDataDay begin\n");
    NetCore_RequestTable("GET_DATA_DAY/\n", NETCORE_STEP_DAY_MS);
}