#include "main.h"

void setup() {
    Serial.begin(115200);
    StartTask();
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
