#include "oledKey.h"

#include <stdio.h>

#include "cmsis_os2.h"
#include "hi_adc.h"
#include "hi_errno.h"
#include "hi_gpio.h"
#include "hi_io.h"
#include "oledDisplay.h"

// 两个按键通过不同分压接到 GPIO5/ADC2，用 ADC 电压区分。
// 若实际左右相反，把下面的 LEFT/RIGHT 处理对调即可。
#define KEY_ADC_CHANNEL   HI_ADC_CHANNEL_2
#define KEY_ADC_GPIO      HI_GPIO_IDX_5
#define KEY_ADC_FUNC      HI_IO_FUNC_GPIO_5_GPIO

#define KEY_SCAN_PERIOD_TICKS 2     // 主循环每 100ms 一次，这里只做防抖计数
#define KEY_STABLE_COUNT   3
#define KEY_IDLE_RATIO     60       // 小于空闲值 60% 视为有按键按下
#define KEY_LOW_RATIO      24       // 实测: 左键约17.5%空闲值, 右键约29.8%, 分界取24%

enum KeyState {
    KEY_STATE_NONE = 0,
    KEY_STATE_LEFT,
    KEY_STATE_RIGHT,
};

static int g_idleReady = 0;
static unsigned int g_idleValue = 0;
static int g_pendingKey = KEY_STATE_NONE;
static int g_stableCount = 0;
static int g_released = 1;
static int g_lastPrinted = -1;

static int KeyClassify(unsigned int value)
{
    unsigned int idle = g_idleValue;
    if (!g_idleReady || idle == 0) {
        return KEY_STATE_NONE;
    }
    if (value >= idle * KEY_IDLE_RATIO / 100) {
        return KEY_STATE_NONE;
    }
    if (value < idle * KEY_LOW_RATIO / 100) {
        return KEY_STATE_LEFT;
    }
    return KEY_STATE_RIGHT;
}

static int KeyReadValue(unsigned int *value)
{
    hi_u16 raw = 0;
    if (hi_adc_read(KEY_ADC_CHANNEL, &raw, HI_ADC_EQU_MODEL_8,
        HI_ADC_CUR_BAIS_DEFAULT, 0) != HI_ERR_SUCCESS) {
        return -1;
    }
    *value = raw;
    return 0;
}

void OledKey_Init(void)
{
    hi_gpio_init();
    hi_io_set_func(KEY_ADC_GPIO, KEY_ADC_FUNC);
    hi_gpio_set_dir(KEY_ADC_GPIO, HI_GPIO_DIR_IN);
    printf("[Key] ADC2 key init ok\n");
}

void OledKey_Scan(void)
{
    unsigned int value = 0;
    int state;
    if (KeyReadValue(&value) != 0) {
        return;
    }

    if (!g_idleReady) {
        g_idleValue = value;
        g_idleReady = 1;
        printf("[Key] idle adc=%u\n", g_idleValue);
        return;
    }

    // 变化较大时打印一次，方便确认两个按键的电压档位
    if (g_lastPrinted < 0 ||
        (value > g_lastPrinted + 20) || (value + 20 < g_lastPrinted)) {
        printf("[Key] adc=%u\n", value);
        g_lastPrinted = (int)value;
    }

    state = KeyClassify(value);
    if (state == KEY_STATE_NONE) {
        g_pendingKey = KEY_STATE_NONE;
        g_stableCount = 0;
        g_released = 1;
        return;
    }

    if (!g_released) {
        return; // 等按键松开，避免长按连翻
    }
    if (g_pendingKey == state) {
        g_stableCount++;
    } else {
        g_pendingKey = state;
        g_stableCount = 1;
    }
    if (g_stableCount >= KEY_STABLE_COUNT) {
        if (state == KEY_STATE_LEFT) {
            printf("[Key] LEFT\n");
            nextPage();
        } else {
            printf("[Key] RIGHT\n");
            preferPage();
        }
        g_pendingKey = KEY_STATE_NONE;
        g_stableCount = 0;
        g_released = 0;
    }
}