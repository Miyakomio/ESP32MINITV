#ifndef _wifilink_h
#define _wifilink_h

#include <Arduino.h>
#include <WiFi.h>

/* 改成你自己的 WiFi 名称和密码后再烧录。 */
static const char* const wifiname = "YOUR_WIFI_SSID";
static const char* const password = "YOUR_WIFI_PASSWORD";

/* 注意：上面必须写 static（或 extern + 在 .cpp 里定义）。
   原来写 const char* wifiname = "..."; 是"变量定义"，
   每个包含本头文件的 .cpp 都会生成一份，链接报
   multiple definition of `wifiname` / `password`。 */

void wifistartlink();          /* 开始连接（不阻塞） */
void reconnectwifi();          /* 掉线后按间隔自动重连 */
bool wifi_is_connected();      /* 当前是否已连接 */

#endif
