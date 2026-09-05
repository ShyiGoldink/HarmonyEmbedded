#ifndef WIFI_IOT_SENSE_SERVICE_H
#define WIFI_IOT_SENSE_SERVICE_H

// 传感器服务入口，由应用主任务调用；内部创建独立任务周期上传数据。
void Sense_Init(void);

#endif