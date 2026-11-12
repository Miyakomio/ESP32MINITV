#ifndef _freertos_run_h
#define _freertos_run_h

#include <Arduino.h>
#include <FreeRTOS.h>
#include <task.h>
#include "TFT/tft_ui.h"

/* ============================================================
   现在这个工程只做一件事：显示时钟

   - 显示任务：tft_ui 驱动屏幕 + LVGL 画时钟
   - 网络任务：预留（以后在这里做 NTP 对时，把时间写进时钟）

   电量/电源管理的模块没有删，仍然留在：
       lib/hardware/power/   （monitor.cpp/h，AXP173 读电量）
       lib/TinyPMU/          （AXP173 驱动）
   以后想加回电量显示，把 monitor 的包含和任务加回来就行。
   ============================================================ */

void StartTask();     /* 在 main.cpp 的 setup() 里调用 */

#endif
