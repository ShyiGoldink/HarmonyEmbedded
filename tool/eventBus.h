#ifndef EVENT_CENTER_H
#define EVENT_CENTER_H

#include <stddef.h>

// ============ 事件类型枚举 ============
typedef enum {
    
    EVENT_NET_TIME_READY,        // 服务器时间已处理完成，arg 指向 netData
    EVENT_NET_TABLE_READY,       // 表格数据已处理完成，arg 指向 netData
    EVENT_COUNT                 // 事件总数（用于数组大小）
} EventType;

// ============ 回调函数类型 ============
// 参数1: 事件类型（可用于同一个回调处理多种事件）
// 参数2: 事件携带的数据（可以是任意指针）
typedef void (*EventHandler)(EventType type, void* arg);

// ============ 对外接口 ============

// 初始化事件中心（系统启动时调用一次）
void Event_Init(void);

// 注册事件监听
void Event_Subscribe(EventType type, EventHandler handler);

// 取消注册（可选，一般不需要）
void Event_Unsubscribe(EventType type, EventHandler handler);

// 触发事件（由事件源模块调用）
void Event_Publish(EventType type, void* arg);

// 触发事件（带延迟，用于中断上下文）
// 在中断里调用 Event_PublishFromISR，在主循环里调用 Event_ProcessPending
void Event_PublishFromISR(EventType type, void* arg);
void Event_ProcessPending(void);

#endif