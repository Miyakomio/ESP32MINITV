#include "buzzer.h"
void buzzer_init(){
    pinMode(BUZZER_PIN,OUTPUT);
    digitalWrite(BUZZER_PIN,HIGH);
}
void buzzer(uint8_t enable){
    if(enable)digitalWrite(BUZZER_PIN,LOW);
    else digitalWrite(BUZZER_PIN,HIGH);
}