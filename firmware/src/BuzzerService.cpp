#include "BuzzerService.h"

BuzzerService::BuzzerService(int pin, int channel, bool activeLow)
    : pin(pin), channel(channel), activeLow(activeLow),
      active(false), stopTime(0),
      beepsLeft(0), beepFreq(1000),
      beepDuration(300), beepGap(150),
      nextBeepTime(0), inGap(false)
{
}

void BuzzerService::begin()
{
    // Đưa chân buzzer về trạng thái nghỉ trước khi gắn LEDC
    // để tránh kêu liên tục/giật lúc ESP32 vừa boot.
    pinMode(pin, OUTPUT);
    digitalWrite(pin, activeLow ? HIGH : LOW);

    ledcSetup(channel, 1000, 8); // channel, freq mặc định, 8-bit resolution
    ledcAttachPin(pin, channel);

    stopTone();

    Serial.print("[BUZZER] Ready | activeLow=");
    Serial.println(activeLow ? "true" : "false");
}

void BuzzerService::startTone(int freq)
{
    ledcSetup(channel, freq, 8);
    ledcWrite(channel, 128); // PWM 50% duty để tạo tiếng bíp
    active = true;
}

void BuzzerService::stopTone()
{
    // Active-high: duty 0 = tắt
    // Active-low : duty 255 = giữ mức HIGH = tắt
    ledcWrite(channel, activeLow ? 255 : 0);
    active = false;
}

void BuzzerService::beep(int freq, int durationMs)
{
    startTone(freq);
    stopTime = millis() + durationMs;
    beepsLeft = 0; // single beep
}

void BuzzerService::mealAlert()
{
    // 2 tiếng bíp: bíp bíp
    beepFreq = 1200;
    beepDuration = 250;
    beepGap = 120;
    beepsLeft = 3;
    inGap = false;

    startTone(beepFreq);
    stopTime = millis() + beepDuration;
}

void BuzzerService::update()
{
    uint32_t now = millis();

    if (beepsLeft > 0)
    {
        if (!inGap && now >= stopTime)
        {
            // Kết thúc tiếng bíp → vào gap
            stopTone();
            beepsLeft--;

            if (beepsLeft > 0)
            {
                inGap = true;
                nextBeepTime = now + beepGap;
            }
        }
        else if (inGap && now >= nextBeepTime)
        {
            // Kết thúc gap → bíp tiếp
            inGap = false;
            startTone(beepFreq);
            stopTime = now + beepDuration;
        }
    }
    else if (active && now >= stopTime)
    {
        // Single beep kết thúc
        stopTone();
    }
}
