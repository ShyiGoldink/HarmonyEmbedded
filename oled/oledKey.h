#ifndef WIFI_IOT_OLED_KEY_H
#define WIFI_IOT_OLED_KEY_H

// 初始化 OLED 翻页按键（中断方式）
void OledKey_Init(void);

// 在主循环中轮询，负责去抖并调用 nextPage/preferPage
void OledKey_Scan(void);

#endif