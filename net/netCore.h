#ifndef WIFI_IOT_NET_CORE_H
#define WIFI_IOT_NET_CORE_H

// 上传：发送传感器温度/湿度
int Send_Sense_Data(float temp, float humidity);

// 拉取：getTime 通过 EVENT_NET_TIME_READY 回调，getData* 通过 EVENT_NET_TABLE_READY 回调
long long getTime(void);
void getDataMinute(void);
void getDataHour(void);
void getDataDay(void);

#endif