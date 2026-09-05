#ifndef WIFI_IOT_TOOL_NETDATA_H
#define WIFI_IOT_TOOL_NETDATA_H

// 为了快速原型实现，使用一个包含所有种类数据的结构体来实现网络数据的数据类型。
typedef struct netData
{
    long long time;      // 服务器时间(毫秒)
    float* tempData;     // 温度
    float* humiData;     // 湿度
    long long* timer;    // 各数据点时间戳(毫秒)
    unsigned int count;  // 有效数据点数量
} netData;

#endif