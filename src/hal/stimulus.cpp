#include "stimulus.h"
#include "pins.h"
#include <Arduino.h>

void Stimulus::begin() {
    ledcSetup(BASE_CHAN, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_BASE, BASE_CHAN);
    ledcSetup(COLLECTOR_CHAN, PWM_FREQ, PWM_RES);
    ledcAttachPin(PIN_PWM_COLLECTOR, COLLECTOR_CHAN);
    baseOff();
    collectorOff();
}

void Stimulus::setBaseDuty(uint16_t duty) {
    m_baseDuty = (duty > 1023) ? 1023 : duty;
    ledcWrite(BASE_CHAN, m_baseDuty);
}

void Stimulus::setCollectorDuty(uint16_t duty) {
    m_collectorDuty = (duty > 1023) ? 1023 : duty;
    ledcWrite(COLLECTOR_CHAN, m_collectorDuty);
}

void Stimulus::baseOff() {
    setBaseDuty(0);
}

void Stimulus::collectorOff() {
    setCollectorDuty(0);
}
