#include "oledDisplay.h"

#include <stdio.h>
#include <string.h>

#include "iot_errno.h"
#include "iot_i2c.h"
#include "iot_gpio_ex.h"
#include "netCore.h"
#include "../tool/eventBus.h"
#include "ssd1306_fonts.h"

#define OLED_I2C_IDX 0
#define OLED_I2C_ADDR 0x78
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_PAGES (OLED_HEIGHT / 8)
#define OLED_GRAPH_POINTS 32
#define OLED_I2C_BAUDRATE (400 * 1000)
#define OLED_GRAPH_LEFT 18
#define OLED_GRAPH_RIGHT 126
#define OLED_GRAPH_TOP 18
#define OLED_GRAPH_BOTTOM 52

page currentPage = INITIAL_PAGE;
bool canChange = true;

static uint8_t g_frame[OLED_WIDTH * OLED_PAGES];
static float g_temperature[OLED_GRAPH_POINTS];
static float g_humidity[OLED_GRAPH_POINTS];
static long long g_time[OLED_GRAPH_POINTS];
static netData g_currentData;
static unsigned int g_dataCount;

static void OledOnNetDataReady(EventType type, void* arg);

static uint32_t OledWrite(uint8_t control, const uint8_t *data, size_t length)
{
    uint8_t buffer[OLED_WIDTH + 1];
    if (length > OLED_WIDTH) {
        return IOT_FAILURE;
    }
    buffer[0] = control;
    memcpy(&buffer[1], data, length);
    return IoTI2cWrite(OLED_I2C_IDX, OLED_I2C_ADDR, buffer, length + 1);
}

static uint32_t OledCommand(uint8_t command)
{
    return OledWrite(0x00, &command, 1);
}

static void OledSetCursor(uint8_t x, uint8_t pageIndex)
{
    OledCommand((uint8_t)(0xB0 | pageIndex));
    OledCommand((uint8_t)(0x00 | (x & 0x0F)));
    OledCommand((uint8_t)(0x10 | (x >> 4)));
}

static void OledSetPixel(uint8_t x, uint8_t y, bool on)
{
    uint32_t index;
    uint8_t mask;
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }
    index = (uint32_t)(y / 8) * OLED_WIDTH + x;
    mask = (uint8_t)(1U << (y & 7));
    if (on) {
        g_frame[index] |= mask;
    } else {
        g_frame[index] &= (uint8_t)~mask;
    }
}

static void OledLine(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1, bool on)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    int nextError;
    for (;;) {
        OledSetPixel(x0, y0, on);
        if (x0 == x1 && y0 == y1) {
            return;
        }
        nextError = 2 * error;
        if (nextError >= dy) {
            error += dy;
            x0 = (uint8_t)(x0 + sx);
        }
        if (nextError <= dx) {
            error += dx;
            y0 = (uint8_t)(y0 + sy);
        }
    }
}

static void OledClear(void)
{
    memset(g_frame, 0, sizeof(g_frame));
}

static void OledChar(uint8_t x, uint8_t pageIndex, char character)
{
    uint8_t glyph;
    uint8_t column;
    uint8_t row;
    if (character < ' ' || character > '~' || x > OLED_WIDTH - 6 || pageIndex >= OLED_PAGES) {
        return;
    }
    glyph = (uint8_t)(character - ' ');
    for (column = 0; column < 6; column++) {
        for (row = 0; row < 8; row++) {
            OledSetPixel((uint8_t)(x + column), (uint8_t)(pageIndex * 8 + row),
                (g_f6X8[glyph][column] & (uint8_t)(1U << row)) != 0);
        }
    }
}

static void OledText(uint8_t x, uint8_t pageIndex, const char *text)
{
    while (text != NULL && *text != '\0' && x <= OLED_WIDTH - 6) {
        OledChar(x, pageIndex, *text++);
        x = (uint8_t)(x + 6);
    }
}

static void OledFlush(void)
{
    uint8_t pageIndex;
    for (pageIndex = 0; pageIndex < OLED_PAGES; pageIndex++) {
        OledSetCursor(0, pageIndex);
        OledWrite(0x40, &g_frame[(uint32_t)pageIndex * OLED_WIDTH], OLED_WIDTH);
    }
}

static void OledDrawTitle(const char *title)
{
    OledText(0, 0, title);
    OledLine(0, 9, OLED_WIDTH - 1, 9, true);
}

static uint8_t GraphY(float value, float minimum, float maximum)
{
    float ratio;
    if (maximum <= minimum) {
        return (OLED_GRAPH_TOP + OLED_GRAPH_BOTTOM) / 2;
    }
    ratio = (value - minimum) / (maximum - minimum);
    if (ratio < 0.0f) {
        ratio = 0.0f;
    }
    if (ratio > 1.0f) {
        ratio = 1.0f;
    }
    return (uint8_t)(OLED_GRAPH_BOTTOM - ratio * (OLED_GRAPH_BOTTOM - OLED_GRAPH_TOP));
}

static void OledDrawGraph(const char *title)
{
    unsigned int i;
    float minimum = 0.0f;
    float maximum = 1.0f;
    bool hasValue = false;
    uint8_t previousX = 0;
    uint8_t previousTemperature = 0;
    uint8_t previousHumidity = 0;

    OledDrawTitle(title);
    OledLine(OLED_GRAPH_LEFT, OLED_GRAPH_TOP, OLED_GRAPH_LEFT, OLED_GRAPH_BOTTOM, true);
    OledLine(OLED_GRAPH_LEFT, OLED_GRAPH_BOTTOM, OLED_GRAPH_RIGHT, OLED_GRAPH_BOTTOM, true);
    for (i = 0; i < g_dataCount; i++) {
        if (!hasValue || g_temperature[i] < minimum) minimum = g_temperature[i];
        if (!hasValue || g_humidity[i] < minimum) minimum = g_humidity[i];
        if (!hasValue || g_temperature[i] > maximum) maximum = g_temperature[i];
        if (!hasValue || g_humidity[i] > maximum) maximum = g_humidity[i];
        hasValue = true;
    }
    if (!hasValue) {
        OledText(30, 4, "NO DATA");
        return;
    }
    if (maximum - minimum < 1.0f) {
        minimum -= 0.5f;
        maximum += 0.5f;
    }
    for (i = 0; i < g_dataCount; i++) {
        uint8_t x = (uint8_t)(OLED_GRAPH_LEFT +
            (i * (OLED_GRAPH_RIGHT - OLED_GRAPH_LEFT)) / (g_dataCount > 1 ? g_dataCount - 1 : 1));
        uint8_t temperature = GraphY(g_temperature[i], minimum, maximum);
        uint8_t humidity = GraphY(g_humidity[i], minimum, maximum);
        if (i > 0) {
            OledLine(previousX, previousTemperature, x, temperature, true);
            if ((i & 1U) == 0) {
                OledLine(previousX, previousHumidity, x, humidity, true);
            }
        }
        OledSetPixel(x, temperature, true);
        OledSetPixel(x, humidity, true);
        previousX = x;
        previousTemperature = temperature;
        previousHumidity = humidity;
    }
    OledText(20, 7, "T:solid H:dash");
}

static void OledFormatTimeMs(long long timeMs, char* dateText, size_t dateSize, char* clockText, size_t clockSize)
{
    long long beijingMs = timeMs + 8LL * 3600LL * 1000LL;
    long long days = beijingMs / 86400000LL;
    long long daySeconds = (beijingMs % 86400000LL) / 1000LL;
    long long z = days + 719468LL;
    long long era = z / 146097LL;
    long long doe = z - era * 146097LL;
    long long yoe = (doe - doe / 1460LL + doe / 36524LL - doe / 146096LL) / 365LL;
    long long year = yoe + era * 400LL;
    long long doy = doe - (365LL * yoe + yoe / 4LL - yoe / 100LL);
    long long mp = (5LL * doy + 2LL) / 153LL;
    long long day = doy - (153LL * mp + 2LL) / 5LL + 1LL;
    long long month = mp + (mp < 10LL ? 3LL : -9LL);
    long long hour = daySeconds / 3600LL;
    long long minute = (daySeconds % 3600LL) / 60LL;
    long long second = daySeconds % 60LL;
    if (month <= 2LL) {
        year += 1LL;
    }
    (void)snprintf(dateText, dateSize, "%04lld-%02lld-%02lld", year, month, day);
    (void)snprintf(clockText, clockSize, "%02lld:%02lld:%02lld", hour, minute, second);
}

static void OledDrawInitial(void)
{
    char dateText[16];
    char clockText[16];
    OledDrawTitle("TEMP/HUMI MONITOR");
    if (g_currentData.time > 0) {
        OledFormatTimeMs(g_currentData.time, dateText, sizeof(dateText), clockText, sizeof(clockText));
        OledText(0, 2, dateText);
        OledText(0, 3, clockText);
    } else {
        OledText(0, 3, "NO TIME");
    }
    OledText(0, 5, "LEFT/RIGHT: PAGE");
}

static void OledDrawCurrentPage(void)
{
    OledClear();
    switch (currentPage) {
        case INITIAL_PAGE:
            OledDrawInitial();
            break;
        case DATA_BY_MINUTE:
            OledDrawGraph("MINUTE DATA");
            break;
        case DATA_BY_HOUR:
            OledDrawGraph("HOUR DATA");
            break;
        case DATA_BY_DAY:
            OledDrawGraph("DAY DATA");
            break;
        default:
            currentPage = INITIAL_PAGE;
            OledDrawInitial();
            break;
    }
    OledFlush();
}

void Oled_Init(void)
{
    static const uint8_t initCommands[] = {
        0xAE, 0x20, 0x02, 0xB0, 0xC8, 0x00, 0x10, 0x40,
        0x81, 0x8F, 0xA1, 0xA6, 0xA8, 0x3F, 0xD3, 0x00,
        0xD5, 0x80, 0xD9, 0xF1, 0xDA, 0x12, 0xDB, 0x40,
        0x8D, 0x14, 0xAF
    };
    size_t i;
    IoSetFunc(IOT_IO_NAME_GPIO_13, IOT_IO_FUNC_GPIO_13_I2C0_SDA);
    IoSetFunc(IOT_IO_NAME_GPIO_14, IOT_IO_FUNC_GPIO_14_I2C0_SCL);
    (void)IoTI2cInit(OLED_I2C_IDX, OLED_I2C_BAUDRATE);
    (void)IoTI2cSetBaudrate(OLED_I2C_IDX, OLED_I2C_BAUDRATE);
    for (i = 0; i < sizeof(initCommands); i++) {
        (void)OledCommand(initCommands[i]);
    }
    OledClear();
    OledDrawCurrentPage();
    Event_Subscribe(EVENT_NET_DATA_READY, OledOnNetDataReady);
    canChange = false;
    getTime();
}

void nextPage(void)
{
    if (!canChange) return;
    currentPage = currentPage == INITIAL_PAGE ? DATA_BY_DAY : (page)(currentPage - 1);
    changePage();
}

void preferPage(void)
{
    if (!canChange) return;
    currentPage = currentPage == DATA_BY_DAY ? INITIAL_PAGE : (page)(currentPage + 1);
    changePage();
}

void changePage(void)
{
    canChange = false;
    OledDrawCurrentPage();
    switch (currentPage) {
        case INITIAL_PAGE:
            getTime();
            break;
        case DATA_BY_MINUTE:
            getDataMinute();
            break;
        case DATA_BY_HOUR:
            getDataHour();
            break;
        case DATA_BY_DAY:
            getDataDay();
            break;
        default:
            break;
    }
}

void freshPage(netData data)
{
    unsigned int i;
    g_currentData.time = data.time;
    g_dataCount = data.count > OLED_GRAPH_POINTS ? OLED_GRAPH_POINTS : data.count;
    for (i = 0; i < g_dataCount; i++) {
        g_temperature[i] = data.tempData == NULL ? 0.0f : data.tempData[i];
        g_humidity[i] = data.humiData == NULL ? 0.0f : data.humiData[i];
        g_time[i] = data.timer == NULL ? (long long)i : data.timer[i];
    }
    g_currentData.tempData = g_temperature;
    g_currentData.humiData = g_humidity;
    g_currentData.timer = g_time;
    g_currentData.count = g_dataCount;
    OledDrawCurrentPage();
    canChange = true;
}

static void OledOnNetDataReady(EventType type, void* arg)
{
    netData* data;
    (void)type;
    if (arg == NULL) {
        return;
    }
    data = (netData*)arg;
    freshPage(*data);
}

netData currentNetData(void)
{
    return g_currentData;
}