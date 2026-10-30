#pragma once
#include "configuration.h"

namespace eko
{
class BadgeAudioBackend
{
  public:
    bool available() const { return EKO_AUDIO_VERIFIED; }
    void setDuty(unsigned value) { duty = value >= 1 && value <= 8 ? value : 1; }
    bool prepare()
    {
        if (!available())
            return false;
        digitalWrite(EKO_AUDIO_SD, HIGH);
        pinMode(EKO_AUDIO_SD, OUTPUT);
        attached = ledcAttach(EKO_AUDIO_PWM, 1000, 10);
        if (!attached || !ledcWrite(EKO_AUDIO_PWM, 0)) {
            stop();
            return false;
        }
        digitalWrite(EKO_AUDIO_SD, LOW);
        return true;
    }
    bool tone(uint32_t frequency)
    {
        return attached && ledcChangeFrequency(EKO_AUDIO_PWM, frequency, 10) && ledcWrite(EKO_AUDIO_PWM, duty);
    }
    bool quiet() { return attached && ledcWrite(EKO_AUDIO_PWM, 0); }
    void stop()
    {
        if (!available())
            return;
        digitalWrite(EKO_AUDIO_SD, HIGH);
        if (attached) {
            ledcWrite(EKO_AUDIO_PWM, 0);
            ledcDetach(EKO_AUDIO_PWM);
            attached = false;
        }
        pinMode(EKO_AUDIO_PWM, INPUT);
    }

  private:
    unsigned duty = 8;
    bool attached = false;
};
} // namespace eko
