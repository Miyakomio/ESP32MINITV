#include "monitor.h"

#include <Wire.h>
#include "TinyPMU.h"        /* 来自 lib/TinyPMU（AXP173 驱动） */

static AXP173 pmu;
static bool   s_ready = false;

void monitor_init(uint8_t sda, uint8_t scl)
{
    Wire.begin(sda, scl);
    s_ready = pmu.begin(&Wire);          /* 返回 false = I2C 上没找到 AXP173 */
    Serial.printf("[PMU] AXP173 %s\r\n", s_ready ? "OK" : "NOT FOUND");

    if (s_ready) {
        /* begin() 只做 I2C 握手，ADC 通道要手动打开 */
        pmu.setADCEnable(AXP173::ADC_BAT_V, true);   /* 电池电压 */
        pmu.setADCEnable(AXP173::ADC_TS,    true);   /* 电池温度 */
    }
}

bool monitor_ready(void) { return s_ready; }

float monitor_voltage(void)
{
    if (!s_ready) {
        return 0.0f;
    }
    /* 采 8 次取平均：WiFi 发射、背光开启会让电池电压瞬时跌落几十毫伏 */
    float sum = 0.0f;
    for (int i = 0; i < 8; i++) {
        sum += pmu.getBatVoltage();      /* 单位：V */
        delay(2);
    }
    return sum / 8.0f;
}

/* 电压 → 电量百分比：锂电池放电曲线（非线性），比线性估算准得多 */
float monitor_percent(void)
{
    if (!s_ready) {
        return 0.0f;
    }

    const int mv = (int)(monitor_voltage() * 1000.0f);

    static const int16_t tbl[][2] = {
        {4200,100},{4100,90},{4000,80},{3930,70},{3870,60},
        {3820,50},{3790,40},{3770,30},{3740,20},{3680,10},
        {3450,5},{3300,0}
    };
    const int n = (int)(sizeof(tbl) / sizeof(tbl[0]));

    if (mv >= tbl[0][0]) {
        return 100.0f;
    }
    for (int i = 1; i < n; i++) {
        if (mv >= tbl[i][0]) {
            const int dv = tbl[i - 1][0] - tbl[i][0];
            const int ds = tbl[i - 1][1] - tbl[i][1];
            return (float)(tbl[i][1] + (mv - tbl[i][0]) * ds / dv);
        }
    }
    return 0.0f;
}

float monitor_percent_linear(void) { return s_ready ? pmu.getBatLevel()  : 0.0f; }
bool  monitor_charging(void)       { return s_ready ? pmu.isCharging()   : false; }
bool  monitor_usb(void)            { return s_ready ? pmu.isVBUSExist()  : false; }
bool  monitor_bat_exist(void)      { return s_ready ? pmu.isBatExist()   : false; }
float monitor_temp(void)           { return s_ready ? pmu.getTSTemp()    : 0.0f; }
