#ifndef BUZZER_SERVICE_H
#define BUZZER_SERVICE_H

#include <Arduino.h>

/*
   BuzzerService
   --------------------------------
   Điều khiển buzzer passive qua PWM
   Non-blocking — gọi update() trong loop()

   activeLow = true:
   - Dành cho module buzzer loại kích mức LOW
   - Trạng thái nghỉ sẽ là HIGH / duty 255
*/
class BuzzerService
{
public:
    BuzzerService(int pin, int channel = 0, bool activeLow = false);
    void begin();

    // Phát 1 tiếng bíp đơn
    void beep(int freq = 1000, int durationMs = 300);

    // Phát vài tiếng bíp ngắn (báo giờ ăn)
    void mealAlert();

    void update(); // Gọi liên tục trong loop()

private:
    int pin;
    int channel;
    bool activeLow;

    // Non-blocking state
    bool active;
    unsigned long stopTime;

    // Multi-beep state
    int beepsLeft;
    int beepFreq;
    int beepDuration;
    int beepGap;
    unsigned long nextBeepTime;
    bool inGap;

    void startTone(int freq);
    void stopTone();
};

#endif
