#include "wifilink.h"

/* ============================================================
   WiFi 连接管理（非阻塞）
   ============================================================ */
static const uint32_t WIFI_RECONNECT_INTERVAL = 10000;   /* 重连间隔 ms */
static uint32_t       s_lastReconnectTime    = 0;

void wifistartlink()
{
    Serial.printf("[WIFI] 开始连接: %s\r\n", wifiname);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiname, password);
    s_lastReconnectTime = millis();
}

bool wifi_is_connected()
{
    return WiFi.status() == WL_CONNECTED;
}

void reconnectwifi()
{
    if (wifi_is_connected()) {
        return;
    }
    if (millis() - s_lastReconnectTime < WIFI_RECONNECT_INTERVAL) {
        return;
    }

    s_lastReconnectTime = millis();
    Serial.println("[WIFI] 掉线，重新连接…");
    WiFi.disconnect();
    WiFi.begin(wifiname, password);
}
