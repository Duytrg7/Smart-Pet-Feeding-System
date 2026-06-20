#ifndef FIREBASE_SERVICE_H
#define FIREBASE_SERVICE_H

#include <Arduino.h>
#include <Firebase_ESP_Client.h>

#include "secrets.h"

class FirebaseService
{
public:
   void begin();
   void update();
   bool isReady();

   /* ── Ghi trạng thái lên Firebase ── */
   void sendHopperWeight(float w);
   void sendFoodState(const char *state);
   void sendLastFed(int h, int m);
   void sendIsFeeding(bool feeding);

   /*
      sendDispensed()
      Ghi lượng thức ăn thực tế vừa xả vào log
      path: petfeeder/status/lastDispensed  (gram thực tế)
            petfeeder/log/{timestamp}       (lịch sử)
   */
   void sendDispensed(const char *mealId, float targetG, float actualG, float trayBefore, int h, int m);

   /* ── Đọc lệnh / cấu hình từ Firebase ── */
   bool getManualFeedCommand(float &grams);
   void setManualFeedStatus(const char *status);
   void getSchedule(int &h0, int &m0, int &h1, int &m1);

   /*
      getGramsPerMeal()
      Đọc khẩu phần gram/bữa từ Firebase
      path: petfeeder/config/gramsPerMeal
      return: gram cần xả mỗi bữa, -1 nếu chưa cài hoặc lỗi
   */
   float getGramsPerMeal();

   /*
      setGramsPerMeal()
      Ghi khẩu phần lên Firebase (gọi từ web sau khi analyze)
      Thường web ghi trực tiếp, hàm này dùng để ESP32 tự ghi nếu cần
   */
   void setGramsPerMeal(float g);

   void sendTrayWeight(float w);                                                             // petfeeder/status/trayWeight
   void sendMealConsumed(const char *mealId, float consumed, float trayAfter, int h, int m); // petfeeder/status/lastMealConsumed

private:
   FirebaseData fbdo;
   FirebaseData fbdo2; // fbdo riêng cho log, tránh conflict
   FirebaseAuth auth;
   FirebaseConfig config;
   bool ready = false;
};

#endif