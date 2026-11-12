#ifndef _monitor_h
#define _monitor_h

// 电池电量检测模块（基于 TINY-PMU 上的 AXP173 电源管理芯片）
// 用法：任务开始前调一次 monitor_init()，之后随时读

#include <Arduino.h>

/* AXP173 的 I2C 接线：按你的板子改这两个 GPIO */
#define PMU_SDA   8
#define PMU_SCL   9

void  monitor_init(uint8_t sda, uint8_t scl);   // 初始化（只调一次）
bool  monitor_ready(void);                      // PMU 通信是否正常

float monitor_voltage(void);        // 电池电压，单位 V
float monitor_percent(void);        // 电量 0~100（查放电曲线，较准）
float monitor_percent_linear(void); // 电量 0~100（库自带的线性估算，偏乐观）

bool  monitor_charging(void);       // 是否正在充电
bool  monitor_usb(void);            // 是否插着 USB
bool  monitor_bat_exist(void);      // 是否接着电池
float monitor_temp(void);           // 电池 NTC 温度，单位 ℃

#endif
