#include "eventBus.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os2.h"

// ============ 配置 ============
#define MAX_HANDLERS_PER_EVENT  5   // 每个事件最多5个订阅者
#define MAX_PENDING_EVENTS      10  // 中断触发的待处理事件队列大小

// ============ 数据结构 ============
typedef struct {
    EventHandler handlers[MAX_HANDLERS_PER_EVENT];
    int count;
} EventSlot;

typedef struct {
    EventType type;
    void* arg;
} PendingEvent;

// ============ 静态存储 ============
static EventSlot g_slots[EVENT_COUNT];
static PendingEvent g_pending[MAX_PENDING_EVENTS];
static int g_pending_head = 0;
static int g_pending_tail = 0;
static int g_pending_count = 0;
static osMutexId_t g_mutex = NULL;

// ============ 内部函数 ============

// 判断队列是否满
static int IsPendingFull(void) {
    return g_pending_count >= MAX_PENDING_EVENTS;
}

// 判断队列是否空
static int IsPendingEmpty(void) {
    return g_pending_count == 0;
}

// ============ 对外接口 ============

void Event_Init(void) {
    // 清空所有事件槽
    memset(g_slots, 0, sizeof(g_slots));
    memset(g_pending, 0, sizeof(g_pending));
    g_pending_head = 0;
    g_pending_tail = 0;
    g_pending_count = 0;
    
    // 创建互斥锁（保护 pending 队列）
    g_mutex = osMutexNew(NULL);
    printf("EventCenter 初始化完成\n");
}

void Event_Subscribe(EventType type, EventHandler handler) {
    if (type >= EVENT_COUNT || handler == NULL) return;
    
    EventSlot* slot = &g_slots[type];
    if (slot->count >= MAX_HANDLERS_PER_EVENT) {
        printf("警告: 事件 %d 的订阅者已满\n", type);
        return;
    }
    
    // 检查是否已经订阅过了（防止重复）
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i] == handler) {
            printf("警告: 事件 %d 已存在该订阅者\n", type);
            return;
        }
    }
    
    slot->handlers[slot->count++] = handler;
    printf("订阅成功: 事件 %d, 当前订阅数 %d\n", type, slot->count);
}

void Event_Unsubscribe(EventType type, EventHandler handler) {
    if (type >= EVENT_COUNT || handler == NULL) return;
    
    EventSlot* slot = &g_slots[type];
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i] == handler) {
            // 用最后一个元素覆盖当前位置
            slot->handlers[i] = slot->handlers[slot->count - 1];
            slot->count--;
            printf("取消订阅: 事件 %d\n", type);
            return;
        }
    }
}

void Event_Publish(EventType type, void* arg) {
    if (type >= EVENT_COUNT) return;
    
    EventSlot* slot = &g_slots[type];
    if (slot->count == 0) return;  // 没有订阅者，直接返回
    
    // 遍历所有订阅者，逐个调用
    for (int i = 0; i < slot->count; i++) {
        if (slot->handlers[i] != NULL) {
            slot->handlers[i](type, arg);
        }
    }
}

// 中断安全版本：把事件放入队列，在主循环处理
void Event_PublishFromISR(EventType type, void* arg) {
    if (IsPendingFull()) {
        printf("警告: pending 事件队列已满，丢弃事件 %d\n", type);
        return;
    }
    
    // 获取锁（中断里用 TryLock，防止死锁）
    if (osMutexAcquire(g_mutex, 0) != osOK) {
        return;  // 获取锁失败，直接丢弃（安全）
    }
    
    g_pending[g_pending_tail].type = type;
    g_pending[g_pending_tail].arg = arg;
    g_pending_tail = (g_pending_tail + 1) % MAX_PENDING_EVENTS;
    g_pending_count++;
    
    osMutexRelease(g_mutex);
}

// 处理队列中的待处理事件（在主循环中调用）
void Event_ProcessPending(void) {
    if (IsPendingEmpty()) return;
    
    if (osMutexAcquire(g_mutex, 0) != osOK) {
        return;  // 获取锁失败，下次再处理
    }
    
    // 取出一个事件并发布
    EventType type = g_pending[g_pending_head].type;
    void* arg = g_pending[g_pending_head].arg;
    g_pending_head = (g_pending_head + 1) % MAX_PENDING_EVENTS;
    g_pending_count--;
    
    osMutexRelease(g_mutex);
    
    // 发布事件（在任务上下文执行，不是中断上下文）
    Event_Publish(type, arg);
}