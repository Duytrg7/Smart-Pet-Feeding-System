#include "Feeder.h"

const float Feeder::OVERSHOOT_MARGIN = 1.00f;

Feeder::Feeder(int pin, int openAngle, int closeAngle)
    : servoPin(pin), openAngle(openAngle), closeAngle(closeAngle),
      feeding(false), openTime(0), closeTime(0), step(IDLE),
      targetGrams(0), initialWeight(-1), dispensed(0),
      dispenseStart(0), settleStart(0), mode(MODE_TIMED), closeHitCount(0),
      pulseStart(0), currentPulseDuration(0), pulseCount(0)
{
}

void Feeder::begin()
{
   servo.setPeriodHertz(50);
   servo.attach(servoPin, 500, 2400);
   servo.write(closeAngle);
   delay(300);
}

// ── Chế độ hẹn giờ (giữ nguyên cho nút bấm) ──
void Feeder::feedOnce()
{
   if (feeding)
      return;
   mode = MODE_TIMED;
   servo.write(openAngle);
   openTime = millis();
   step = OPENING_TIMED;
   feeding = true;
   Serial.println("[FEEDER] feedOnce: Opening");
}

uint32_t Feeder::choosePulseDuration(float remaining)
{
   // Target nhỏ như 7g thì dùng nhịp rất ngắn
   if (targetGrams <= 10.0f)
   {
      if (remaining > 4.0f)
         return PULSE_MEDIUM; // 80ms
      return PULSE_SHORT;     // 50ms
   }

   // Target trung bình/lớn
   if (remaining > 15.0f)
      return PULSE_LONG; // 120ms

   if (remaining > 6.0f)
      return PULSE_MEDIUM; // 80ms

   return PULSE_SHORT; // 50ms khi gần đạt mục tiêu
}

void Feeder::startPulse(uint32_t now)
{
   float remaining = targetGrams - dispensed;

   if (remaining < 0)
      remaining = 0;

   currentPulseDuration = choosePulseDuration(remaining);

   servo.write(openAngle);
   pulseStart = now;
   pulseCount++;
   step = PULSE_OPEN;

   Serial.printf("[FEEDER] Pulse %d | open %lums | remaining %.1fg\n",
                 pulseCount,
                 currentPulseDuration,
                 remaining);
}

// ── Chế độ theo khối lượng ──
void Feeder::feedAmount(float target, float initW)
{
   if (feeding)
      return;

   if (target <= 0 || initW < 0)
   {
      Serial.println("[FEEDER] feedAmount: Tham số không hợp lệ");
      return;
   }

   mode = MODE_WEIGHT;
   targetGrams = target;
   initialWeight = initW;
   dispensed = 0;
   closeHitCount = 0;
   pulseCount = 0;

   dispenseStart = millis();
   feeding = true;

   Serial.printf("[FEEDER] feedAmount pulse mode: Mục tiêu %.1fg, hộp hiện tại %.1fg\n",
                 targetGrams, initialWeight);

   startPulse(dispenseStart);
}

void Feeder::update(float hopperWeight)
{
   if (!feeding)
      return;
   uint32_t now = millis();

   switch (step)
   {
   // ────────────── TIMED MODE ──────────────
   case OPENING_TIMED:
      if (now - openTime >= OPEN_DURATION)
      {
         servo.write(closeAngle);
         closeTime = now;
         step = CLOSING_TIMED;
         Serial.println("[FEEDER] feedOnce: Closing");
      }
      break;

   case CLOSING_TIMED:
      if (now - closeTime >= CLOSE_DURATION)
      {
         feeding = false;
         step = IDLE;
         Serial.println("[FEEDER] feedOnce: Done");
      }
      break;

   // ────────────── WEIGHT MODE ──────────────
   case PULSE_OPEN:
   {
      // Timeout an toàn
      if (now - dispenseStart >= DISPENSE_TIMEOUT || pulseCount > MAX_PULSES)
      {
         Serial.println("[FEEDER] TIMEOUT/MAX PULSE! Đóng khẩn cấp");
         servo.write(closeAngle);
         settleStart = now;
         step = SETTLING;
         break;
      }

      // Hết thời gian mở của 1 nhịp → đóng lại
      if (now - pulseStart >= currentPulseDuration)
      {
         servo.write(closeAngle);
         settleStart = now;
         step = PULSE_SETTLE;

         Serial.println("[FEEDER] Pulse close, waiting settle");
      }
      break;
   }

   case PULSE_SETTLE:
   {
      // Timeout an toàn
      if (now - dispenseStart >= DISPENSE_TIMEOUT || pulseCount > MAX_PULSES)
      {
         Serial.println("[FEEDER] TIMEOUT/MAX PULSE during settle! Finish");
         servo.write(closeAngle);
         settleStart = now;
         step = SETTLING;
         break;
      }

      // Chờ đồ ăn rơi ổn định sau khi đóng servo
      if (now - settleStart < PULSE_SETTLE_DURATION)
         break;

      // Cần có cân nặng hợp lệ
      if (hopperWeight < 0)
         break;

      float dropped = initialWeight - hopperWeight;

      if (dropped < 0)
         dropped = 0;

      dispensed = dropped;

      Serial.printf("[FEEDER] Pulse check: %.1fg / %.1fg\n",
                    dispensed, targetGrams);

      // Đạt ngưỡng thì kết thúc
      if (dispensed >= targetGrams * OVERSHOOT_MARGIN)
      {
         servo.write(closeAngle);
         settleStart = now;
         step = SETTLING;

         Serial.printf("[FEEDER] Đạt ngưỡng pulse %.0f%% → Kết thúc xả\n",
                       OVERSHOOT_MARGIN * 100);
      }
      else
      {
         // Chưa đủ thì mở tiếp một nhịp
         startPulse(now);
      }

      break;
   }

   case SETTLING:
      // Chờ thức ăn rơi nốt, loadcell ổn định
      if (now - settleStart >= SETTLE_DURATION)
      {
         // Tính lượng thực tế xả được
         if (hopperWeight >= 0)
         {
            dispensed = initialWeight - hopperWeight;

            if (dispensed < 0)
               dispensed = 0;
         }
         else
         {
            dispensed = targetGrams;
         }

         feeding = false;
         step = IDLE;
         Serial.printf("[FEEDER] Hoàn thành. Thực tế: %.1fg / Mục tiêu: %.1fg\n",
                       dispensed, targetGrams);
      }
      break;

   default:
      break;
   }
}

bool Feeder::isFeeding()
{
   return feeding;
}

float Feeder::getDispensed()
{
   return dispensed;
}