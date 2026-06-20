#ifndef FEEDER_H
#define FEEDER_H

#include <Arduino.h>
#include <ESP32Servo.h>

class Feeder
{
public:
   Feeder(int pin, int openAngle, int closeAngle = 0);
   void begin();

   // Chế độ cũ: mở cứng theo thời gian (dùng cho nút bấm vật lý)
   void feedOnce();

   // Chế độ mới: xả đúng lượng gram, dùng feedback từ loadcell hộp
   // targetGrams  : số gram cần xả
   // initialWeight: cân nặng hộp TẠI THỜI ĐIỂM bắt đầu xả
   void feedAmount(float targetGrams, float initialWeight);

   // Gọi liên tục trong loop()
   // hopperWeight: cân nặng hộp hiện tại (-1 nếu không có)
   void update(float hopperWeight = -1);

   bool isFeeding();
   float getDispensed(); // Gram thực tế đã xả trong lần gần nhất

private:
   Servo servo;
   int servoPin;
   int openAngle;
   int closeAngle;

   bool feeding;

   // ── Chế độ hẹn giờ ──
   uint32_t openTime;
   uint32_t closeTime;
   static const uint32_t OPEN_DURATION = 1000;  // ms giữ mở
   static const uint32_t CLOSE_DURATION = 1000; // ms sau khi đóng

   // ── Chế độ theo khối lượng ──
   float targetGrams;
   float initialWeight;
   float dispensed;
   uint32_t dispenseStart; // Thời điểm bắt đầu xả
   uint32_t settleStart;   // Thời điểm bắt đầu settling

   uint8_t closeHitCount;

   // Đóng sớm ở 85% để tránh overshoot
   static const float OVERSHOOT_MARGIN; // = 0.85f
   // Timeout nếu loadcell không phản hồi
   static const uint32_t DISPENSE_TIMEOUT = 15000; // 15s
   // Chờ thức ăn rơi nốt sau khi đóng
   static const uint32_t SETTLE_DURATION = 1200; // ms
   static const uint8_t CLOSE_HIT_REQUIRED = 2;
   static const uint32_t MIN_DISPENSE_TIME = 500; // ms

   uint32_t pulseStart;
   uint32_t currentPulseDuration;
   uint16_t pulseCount;

   uint32_t choosePulseDuration(float remaining);
   void startPulse(uint32_t now);

   // ── Chế độ xả từng nhịp ──
   static const uint32_t PULSE_SETTLE_DURATION = 800; // ms chờ sau mỗi nhịp
   static const uint32_t PULSE_LONG = 120;            // ms
   static const uint32_t PULSE_MEDIUM = 80;           // ms
   static const uint32_t PULSE_SHORT = 50;            // ms
   static const uint16_t MAX_PULSES = 40;

   enum FeedMode
   {
      MODE_TIMED,
      MODE_WEIGHT
   };
   FeedMode mode;

   enum FeedStep
   {
      IDLE,
      // Timed
      OPENING_TIMED,
      CLOSING_TIMED,
      // Weight-based pulse dosing
      PULSE_OPEN,
      PULSE_SETTLE,
      SETTLING
   };
   FeedStep step;
};

#endif