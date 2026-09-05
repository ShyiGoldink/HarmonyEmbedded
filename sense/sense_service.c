#include "sense_service.h"

#include <stdio.h>

#include "cmsis_os2.h"
#include "aht20.h"
#include "../net/netCore.h"

#define SENSE_TASK_STACK_SIZE (1024 * 8)
#define SENSE_TASK_PRIO 25
#define SENSE_UPLOAD_INTERVAL_TICKS 500  // 5000ms (Hi3861: 100 tick/s)
#define AHT20_CALIBRATE_RETRY 20
#define AHT20_RETRY_DELAY_TICKS 5   // 50ms

static void SenseTask(void *arg)
{
    float temp = 0.0f;
    float humi = 0.0f;
    uint32_t ret = 1;
    int retry = 0;
    (void)arg;

    // OLED 初始化时已把 GPIO13/14 配为 I2C0 并完成 IoTI2cInit(0)，AHT20 与 OLED 共用 I2C0
    while (ret != 0 && retry < AHT20_CALIBRATE_RETRY) {
        ret = AHT20_Calibrate();
        if (ret != 0) {
            printf("[Sense] AHT20 calibrate failed, retry %d\r\n", retry + 1);
            osDelay(AHT20_RETRY_DELAY_TICKS);
        }
        retry++;
    }
    if (ret != 0) {
        printf("[Sense] AHT20 init failed!\r\n");
        return;
    }

    while (1) {
        temp = 0.0f;
        humi = 0.0f;
        ret = AHT20_StartMeasure();
        if (ret != 0) {
            printf("[Sense] AHT20 start measure failed, ret=%u\r\n", (unsigned int)ret);
        } else {
            ret = AHT20_GetMeasureResult(&temp, &humi);
            if (ret != 0) {
                printf("[Sense] AHT20 read failed, ret=%u\r\n", (unsigned int)ret);
            } else {
                printf("[Sense] Temp=%.2f Humi=%.2f\r\n", temp, humi);
                if (Send_Sense_Data(temp, humi) != 0) {
                    printf("[Sense] upload failed\r\n");
                }
            }
        }
        osDelay(SENSE_UPLOAD_INTERVAL_TICKS);
    }
}

void Sense_Init(void)
{
    osThreadAttr_t attr;

    attr.name = "SenseTask";
    attr.attr_bits = 0U;
    attr.cb_mem = NULL;
    attr.cb_size = 0U;
    attr.stack_mem = NULL;
    attr.stack_size = SENSE_TASK_STACK_SIZE;
    attr.priority = SENSE_TASK_PRIO;

    if (osThreadNew((osThreadFunc_t)SenseTask, NULL, &attr) == NULL) {
        printf("[Sense] Failed to create SenseTask!\n");
    }
}