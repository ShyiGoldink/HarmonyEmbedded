#include "oledKey.h"

#include <stdio.h>

#include "cmsis_os2.h"
#include "iot_gpio.h"
#include "iot_gpio_ex.h"
#include "oledDisplay.h"

// 左键/上一页、右键/下一页。若实际接线不是 GPIO11/12，改这两个宏即可。
#define KEY_LEFT_GPIO  11
#define KEY_RIGHT_GPIO 12

#define KEY_DEBOUNCE_TICKS 5 // 约 50ms（100 tick/s）

static volatile int g_leftPressed = 0;
static volatile int g_rightPressed = 0;
static uint32_t g_lastActionTick = 0;

static void KeyLeftIsr(char *arg)
{
    (void)arg;
    g_leftPressed = 1;
}

static void KeyRightIsr(char *arg)
{
    (void)arg;
    g_rightPressed = 1;
}

static void KeyInitOne(unsigned int gpio, unsigned char gpioFunc, void (*isr)(char *))
{
    IoTGpioInit(gpio);
    IoSetFunc(gpio, gpioFunc);
    IoTGpioSetDir(gpio, IOT_GPIO_DIR_IN);
    IoSetPull(gpio, IOT_IO_PULL_UP);
    IoTGpioRegisterIsrFunc(gpio, IOT_INT_TYPE_EDGE, IOT_GPIO_EDGE_FALL_LEVEL_LOW, isr, NULL);
}

void OledKey_Init(void)
{
    KeyInitOne(KEY_LEFT_GPIO, IOT_IO_FUNC_GPIO_11_GPIO, KeyLeftIsr);
    KeyInitOne(KEY_RIGHT_GPIO, IOT_IO_FUNC_GPIO_12_GPIO, KeyRightIsr);
    printf("[Key] left=GPIO%d right=GPIO%d init ok\n", KEY_LEFT_GPIO, KEY_RIGHT_GPIO);
}

void OledKey_Scan(void)
{
    uint32_t now = osKernelGetTickCount();
    if (g_leftPressed && (now - g_lastActionTick) >= KEY_DEBOUNCE_TICKS) {
        g_leftPressed = 0;
        g_lastActionTick = now;
        nextPage();
    }
    if (g_rightPressed && (now - g_lastActionTick) >= KEY_DEBOUNCE_TICKS) {
        g_rightPressed = 0;
        g_lastActionTick = now;
        preferPage();
    }
}