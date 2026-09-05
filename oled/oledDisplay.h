#ifndef WIFI_IOT_OLED_DISPLAY_H
#define WIFI_IOT_OLED_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>
#include "../tool/netData.h"

/*
oled一共需要展示四个页面，这四个页面的含义分别为：
初始页面，
按照分钟记的温度折线图（实线）和湿度折线图（虚线），
按照小时记的温度折线图（实线）和湿度折线图（虚线），
按照天记的温度折线图（实线）和湿度折线图（虚线），
*/
typedef enum {
    INITIAL_PAGE,
    DATA_BY_MINUTE,
    DATA_BY_HOUR,
    DATA_BY_DAY
} page;

/*当前页面*/
extern page currentPage;
/*能不能切换页面的标志位，调用changePage后变为false，此后不允许切换页面
页面刷新结束之后变为true，此时才能切换页面
这个标志位是为了防止用户频繁切换导致接受网络数据的时序问题
*/
extern bool canChange;

/* OLED 初始化入口，由应用启动任务调用。 */
void Oled_Init(void);

/**
 * oled屏幕下有两个按钮，点击左侧按钮调用此函数
 * 这个函数进入上个页面，执行切换页面逻辑
 */
void nextPage(void);
/**
 * oled屏幕下有两个按钮，点击右侧按钮调用此函数
 * 这个函数进入下一个页面（INITIAL -> MINUTE -> HOUR -> DAY -> INITIAL），
 * 执行切换页面逻辑
 */
void preferPage(void);
/**
 * 切换页面逻辑
 * 根据currentPage刷新不受服务器管理的显示，并调用对应的网络服务模块
 * Initpage需要调用netCore的getTime；
 *  DATA_BY_MINUTE,
    DATA_BY_HOUR,
    DATA_BY_DAY
    分别调用netCore的getDataMinute，getDataHour和getDataDay
    切换页面只负责调用上述协议，上述协议调用成功后，通过EventBus调用freshPage函数
 */
void changePage(void);

/**
 * 在获取数据的方法调用结束后，通过eventBus调用该函数来进行页面刷新
 * 首先是要判断当前页面，然后再清空不使用的数据减少内存开销
 * 再根据传入的数据更新netData，并绘制当前UI
 */
void freshPage(netData data);

netData currentNetData(void);

#endif

