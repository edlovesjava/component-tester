#pragma once
#include <stdint.h>

// PWM stimulus: two ESP32 LEDC channels driving RC low-pass filters to act as
// coarse DACs for swept-parameter measurements (variable base bias, variable
// collector rail). Used by phase-2+ curve-tracer code; NOT exercised by
// Identifier::identify(). See transistor-tester-spec.md §4 for the eventual
// front-end this feeds.
class Stimulus {
public:
    static constexpr uint8_t BASE_CHAN = 0;
    static constexpr uint8_t COLLECTOR_CHAN = 1;
    static constexpr uint32_t PWM_FREQ = 5000;
    static constexpr uint8_t PWM_RES = 10;

    void begin();
    void setBaseDuty(uint16_t duty);
    void setCollectorDuty(uint16_t duty);
    void baseOff();
    void collectorOff();
    uint16_t baseDuty() const { return m_baseDuty; }
    uint16_t collectorDuty() const { return m_collectorDuty; }

private:
    uint16_t m_baseDuty = 0;
    uint16_t m_collectorDuty = 0;
};
