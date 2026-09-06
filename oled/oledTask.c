// main.c

#include <stdio.h>
#include "ohos_init.h"
#include "cmsis_os2.h"

#include "../tool/eventBus.h"
#include "../net/netServer.h"
#include "oledDisplay.h"
#include "oledKey.h"
#include "../sense/sense_service.h"



// ============ WiFi 和服务器配置 ============
#define WIFI_SSID       "SHYI"
#define WIFI_PASSWORD   "QIQIQIQI"
#define SERVER_IP       "124.221.189.156"
#define SERVER_PORT     8888

// ============ 主任务栈与优先级（参照工程内样例） ============
#define MAIN_TASK_STACK_SIZE (1024 * 4)
#define MAIN_TASK_PRIO 25

// ============ 系统初始化 ============
static void System_Init(void) {
    // 1. 事件中心
    Event_Init();
    // 2. 启动网络（Oled显示和传感器都依赖网络服务）
    Net_Init(WIFI_SSID, WIFI_PASSWORD, SERVER_IP, SERVER_PORT);
    // 3. Oled 显示
    Oled_Init();
    // 4. 翻页按键
    OledKey_Init();
    // 5. 传感器
    Sense_Init();
}

// ============ 主任务 ============
static void MainTask(void *arg) {
    (void)arg;

    // 初始化所有模块
    System_Init();
    // 主循环
    while (1) {
        // 翻页按键：中断只置标志，任务上下文执行 nextPage/preferPage
        OledKey_Scan();
        // 处理中断产生的事件（按键等）
        Event_ProcessPending();
        // 让出CPU，由操作系统调度其他任务
        osDelay(2); // 约20ms，保证按键轮询与事件处理更及时
    }
}

// ============ 鸿蒙入口 ============
// 按工程样例标准：SYS_RUN 入口只负责创建线程并返回，
// 不要把死循环直接放在 SYS_RUN 回调里执行。
static void MainEntry(void)
{
    osThreadAttr_t attr;

    attr.name = "MainTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = MAIN_TASK_STACK_SIZE;
    attr.priority = MAIN_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)MainTask, NULL, &attr) == NULL) {
        printf("[MainTask] Failed to create MainTask!\n");
    }
}

SYS_RUN(MainEntry);