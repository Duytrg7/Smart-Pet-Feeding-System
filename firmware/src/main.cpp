#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <math.h>

#include "FeedSchedule.h"
#include "TimeService.h"
#include "Feeder.h"
#include "WeightSensor.h"
#include "FirebaseService.h"
#include "BuzzerService.h"

/* ── Pin definitions ── */
#define RTC_CLK 14
#define RTC_DAT 27
#define RTC_RST 26

#define SERVO_PIN 32
#define SERVO_OPEN_ANGLE 45

/* ── Calibration factors ── */
#define HOPPER_CALIB_FACTOR 410.13f // Loadcell 5kg - hộp chứa
#define TRAY_CALIB_FACTOR -374.17f  // Loadcell 5kg - khay ăn

#define HX711_DOUT 25 // Loadcell hộp
#define HX711_SCK 33

#define TRAY_DOUT 23 // Loadcell khay (mới)
#define TRAY_SCK 22

#define FORCE_TARE_ON_BOOT false

#define BUTTON_PIN 18
#define BUZZER_PIN 17           // Buzzer passive
#define BUZZER_ACTIVE_LOW false // true nếu buzzer kêu liên tục khi mức LOW

/* ── Local config storage ── */
Preferences configPrefs;

float gramsPerMeal = 0.0f;

#define MIN_GRAMS_PER_FEED 5.0f
#define MAX_SAFE_GRAMS_PER_FEED 300.0f

#define HOPPER_FAST_MAX_STEP_G 40.0f

struct PendingMealMeasure
{
  bool active;
  char mealId[48];
  float trayBefore;
  float dispensed;
  uint32_t feedEndTime;
  int hour;
  int minute;
};

char currentMealId[48] = "";

PendingMealMeasure pendingMeal = {
    false,
    "",
    0.0f,
    0.0f,
    0,
    0,
    0};

/* ── WiFi ── */
const char *ssid = WIFI_SSID;
const char *pass = WIFI_PASS;

/* ── Objects ── */
FeedSchedule schedule;
TimeService timeService(RTC_DAT, RTC_CLK, RTC_RST);
Feeder feeder(SERVO_PIN, SERVO_OPEN_ANGLE);
WeightSensor hopperSensor("hopper", HX711_DOUT, HX711_SCK, HOPPER_CALIB_FACTOR);
WeightSensor traySensor("tray", TRAY_DOUT, TRAY_SCK, TRAY_CALIB_FACTOR);
FirebaseService firebase;
BuzzerService buzzer(BUZZER_PIN, 7, BUZZER_ACTIVE_LOW); // GPIO17, ledc channel 7

/* ── Timing ── */
unsigned long lastWeightCheck = 0;
unsigned long lastScheduleSync = 0;
unsigned long lastCommandCheck = 0;

const unsigned long WEIGHT_INTERVAL = 3000;
const unsigned long SCHEDULE_INTERVAL = 10000;
const unsigned long COMMAND_INTERVAL = 1500;

#define DEMO_MODE true

#if DEMO_MODE
const unsigned long MEAL_MEASURE_DELAY = 30UL * 1000UL; // 30 giây cho demo
#else
const unsigned long MEAL_MEASURE_DELAY = 15UL * 60UL * 1000UL; // 15 phút thực tế
#endif

const unsigned long FEED_WEIGHT_INTERVAL = 70; // ms
unsigned long lastFeedWeightRead = 0;

// Wifi Related
const unsigned long WIFI_CONNECT_TIMEOUT = 15000; // 15s
const unsigned long WIFI_RETRY_INTERVAL = 30000;  // 30s

unsigned long lastWiFiRetry = 0;
bool firebaseStarted = false;
bool ntpConfigured = false;

// Tracking bữa ăn để đo consumed sau MEAL_MEASURE_DELAY
float trayBeforeMeal = 0;
int mealHour = 0, mealMinute = 0;
bool currentFeedIsWeightBased = false;
float currentFeedTargetGrams = 0;
bool currentFeedFromManual = false;

void makeMealId(RtcDateTime now, const char *source)
{
  snprintf(currentMealId, sizeof(currentMealId),
           "%04d-%02d-%02d_%02d-%02d-%02d_%s",
           now.Year(), now.Month(), now.Day(),
           now.Hour(), now.Minute(), now.Second(),
           source);
}

void startOnlineServices()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  if (!ntpConfigured)
  {
    timeService.setupNTP();
    ntpConfigured = true;
    Serial.println("[NTP] Configured");
  }

  if (!firebaseStarted)
  {
    firebase.begin();
    firebaseStarted = true;
    Serial.println("[FIREBASE] Started");

    firebase.sendIsFeeding(feeder.isFeeding());
  }
}

void connectWiFiWithTimeout()
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.print("[WIFI] Connecting");

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < WIFI_CONNECT_TIMEOUT)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n[WIFI] Connected");
    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    startOnlineServices();
  }
  else
  {
    Serial.println("\n[WIFI] Failed. Continue in OFFLINE mode");
  }
}

bool isOnline()
{
  return WiFi.status() == WL_CONNECTED && firebase.isReady();
}

void handleSerialCommand()
{
  if (!Serial.available())
    return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "tare_hopper")
  {
    Serial.println("[CMD] Tare hopper");
    hopperSensor.tare(20);
  }
  else if (cmd == "tare_tray")
  {
    Serial.println("[CMD] Tare tray");
    traySensor.tare(20);
  }
  else if (cmd == "tare_all")
  {
    Serial.println("[CMD] Tare all scales");
    hopperSensor.tare(20);
    traySensor.tare(20);
  }
  else if (cmd == "raw")
  {
    Serial.print("[RAW] Hopper: ");
    Serial.print(hopperSensor.getRawAverage(10));
    Serial.print(" | Tray: ");
    Serial.println(traySensor.getRawAverage(10));
  }
  else
  {
    Serial.println("[CMD] Unknown command");
    Serial.println("Available: tare_hopper, tare_tray, tare_all, raw");
  }
}

bool isValidGramsPerFeed(float g)
{
  return g >= MIN_GRAMS_PER_FEED && g <= MAX_SAFE_GRAMS_PER_FEED;
}

void loadLocalConfig()
{
  configPrefs.begin("pet_config", true);

  gramsPerMeal = configPrefs.getFloat("gramsMeal", 0.0f);

  configPrefs.end();

  if (isValidGramsPerFeed(gramsPerMeal))
  {
    Serial.printf("[CONFIG] Loaded gramsPerMeal from NVS: %.1fg\n", gramsPerMeal);
  }
  else
  {
    Serial.println("[CONFIG] No valid gramsPerMeal in NVS");
    gramsPerMeal = 0.0f;
  }
}

void saveGramsPerMeal(float g)
{
  if (!isValidGramsPerFeed(g))
  {
    Serial.printf("[CONFIG] Invalid gramsPerMeal ignored: %.1fg\n", g);
    return;
  }

  if (fabsf(g - gramsPerMeal) < 0.1f)
    return;

  gramsPerMeal = g;

  configPrefs.begin("pet_config", false);
  configPrefs.putFloat("gramsMeal", gramsPerMeal);
  configPrefs.end();

  Serial.printf("[CONFIG] Saved gramsPerMeal to NVS: %.1fg\n", gramsPerMeal);
}

void startPendingMealMeasure(const char *mealId,
                             float trayBefore,
                             float dispensed,
                             int h,
                             int m)
{
  // Nếu vẫn còn bữa cũ đang chờ đo, kết thúc sớm bữa cũ để tránh ghi đè
  if (pendingMeal.active)
  {
    Serial.println("[MEAL] Warning: previous pending meal exists. Closing it early.");

    float trayNow = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();
    float consumed = pendingMeal.trayBefore + pendingMeal.dispensed - trayNow;

    if (consumed < 0)
      consumed = 0;

    if (isOnline())
    {
      firebase.sendMealConsumed(
          pendingMeal.mealId,
          consumed,
          trayNow,
          pendingMeal.hour,
          pendingMeal.minute);
    }

    Serial.printf("[MEAL] Previous meal closed early. Consumed: %.1fg\n", consumed);
  }

  pendingMeal.active = true;
  strncpy(pendingMeal.mealId, mealId, sizeof(pendingMeal.mealId) - 1);
  pendingMeal.mealId[sizeof(pendingMeal.mealId) - 1] = '\0';

  pendingMeal.trayBefore = trayBefore;
  pendingMeal.dispensed = dispensed;
  pendingMeal.feedEndTime = millis();
  pendingMeal.hour = h;
  pendingMeal.minute = m;

  Serial.printf("[MEAL] Pending measure started. ID=%s, trayBefore=%.1fg, dispensed=%.1fg\n",
                pendingMeal.mealId,
                pendingMeal.trayBefore,
                pendingMeal.dispensed);
}

void setup()
{
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  loadLocalConfig();
  schedule.load();
  timeService.begin();

  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);
  delay(300);
  feeder.begin();
  hopperSensor.begin(FORCE_TARE_ON_BOOT);
  traySensor.begin(FORCE_TARE_ON_BOOT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, BUZZER_ACTIVE_LOW ? HIGH : LOW); // nếu buzzer active-high
  buzzer.begin();

  connectWiFiWithTimeout();
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    if (millis() - lastWiFiRetry >= WIFI_RETRY_INTERVAL)
    {
      lastWiFiRetry = millis();

      Serial.println("[WIFI] Retry connection...");
      WiFi.disconnect();
      WiFi.begin(ssid, pass);
    }
  }
  else
  {
    startOnlineServices();
  }
  static bool lastFeedingState = false;
  static bool wasFeeding = false;

  bool online = isOnline();

  // ── Cập nhật non-blocking ──
  float hopperWeightForFeeder = hopperSensor.getWeight();

  if (feeder.isFeeding() &&
      millis() - lastFeedWeightRead >= FEED_WEIGHT_INTERVAL)
  {
    lastFeedWeightRead = millis();

    if (hopperSensor.isReady())
    {
      hopperWeightForFeeder = hopperSensor.measureFastFiltered(HOPPER_FAST_MAX_STEP_G);

      Serial.printf("[FEED-WEIGHT] Hopper: %.1fg\n", hopperWeightForFeeder);
    }
  }
  handleSerialCommand();

  feeder.update(hopperWeightForFeeder);

  buzzer.update();

  // ── Đồng bộ thời gian ──
  if (WiFi.status() == WL_CONNECTED && ntpConfigured)
  {
    timeService.syncFromInternet();
  }

  RtcDateTime now = timeService.now();
  int h = now.Hour();
  int m = now.Minute();
  int d = now.Day();

  schedule.resetDaily(d);

  // ── Kiểm tra lịch cho ăn ──
  if (!feeder.isFeeding())
  {
    for (uint8_t i = 0; i < MAX_FEEDS; i++)
    {
      if (schedule.isTimeToFeed(i, h, m))
      {
        trayBeforeMeal = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();
        float currentW = hopperSensor.isReady() ? hopperSensor.measure() : hopperSensor.getWeight();

        bool useWeightBased = isValidGramsPerFeed(gramsPerMeal) && currentW > 0;

        mealHour = h;
        mealMinute = m;
        makeMealId(now, "schedule");

        currentFeedIsWeightBased = useWeightBased;
        currentFeedTargetGrams = useWeightBased ? gramsPerMeal : 0;
        currentFeedFromManual = false;

        if (online)
        {
          firebase.sendLastFed(h, m);
          firebase.sendIsFeeding(true);
        }

        if (useWeightBased)
        {
          feeder.feedAmount(gramsPerMeal, currentW);
        }
        else
        {
          feeder.feedOnce();
        }

        break;
      }
    }
  }

  // ── Cập nhật isFeeding ──
  bool nowFeeding = feeder.isFeeding();
  if (nowFeeding != lastFeedingState)
  {
    if (online && !nowFeeding)
    {
      firebase.sendIsFeeding(false);
    }

    lastFeedingState = nowFeeding;

    Serial.println(nowFeeding ? "[FEEDER] ĐANG MỞ" : "[FEEDER] ĐÃ ĐÓNG");
  }

  // ── Ghi log + bắt đầu đếm thời gian đo lượng ăn khi feeder vừa xong ──
  if (wasFeeding && !nowFeeding)
  {
    if (currentFeedIsWeightBased && currentFeedTargetGrams > 0)
    {
      float gramsDispensedActual = feeder.getDispensed();

      if (online)
      {
        firebase.sendDispensed(
            currentMealId,
            currentFeedTargetGrams,
            gramsDispensedActual,
            trayBeforeMeal,
            mealHour,
            mealMinute);

        if (currentFeedFromManual)
        {
          firebase.setManualFeedStatus("done");
        }
      }
      startPendingMealMeasure(
          currentMealId,
          trayBeforeMeal,
          gramsDispensedActual,
          mealHour,
          mealMinute);

      Serial.printf("[FEED] Weight-based feed done: %.1fg / %.1fg\n",
                    gramsDispensedActual, currentFeedTargetGrams);

      Serial.printf("[TRAY] Chờ %.0f giây để đo lượng ăn. Khay trước: %.1fg\n",
                    MEAL_MEASURE_DELAY / 1000.0f,
                    trayBeforeMeal);
    }
    else
    {
      Serial.println("[FEED] Timed/fallback feed done");

      float trayAfterFeed = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();
      float estimatedDispensed = trayAfterFeed - trayBeforeMeal;

      if (estimatedDispensed < 0)
        estimatedDispensed = 0;

      if (online)
      {
        firebase.sendDispensed(
            currentMealId,
            currentFeedTargetGrams, // 0 nếu button, manualGrams nếu manual fallback
            estimatedDispensed,
            trayBeforeMeal,
            mealHour,
            mealMinute);

        if (currentFeedFromManual)
        {
          firebase.setManualFeedStatus("done_fallback");
        }
      }

      startPendingMealMeasure(
          currentMealId,
          trayBeforeMeal,
          estimatedDispensed,
          mealHour,
          mealMinute);

      Serial.printf("[FEED] Timed/fallback estimated dispensed: %.1fg, target: %.1fg\n",
                    estimatedDispensed,
                    currentFeedTargetGrams);
    }

    buzzer.mealAlert();
    currentFeedIsWeightBased = false;
    currentFeedTargetGrams = 0;
    currentFeedFromManual = false;
  }

  wasFeeding = nowFeeding;

  // ── Đo lượng tiêu thụ sau MEAL_MEASURE_DELAY ──
  if (pendingMeal.active &&
      millis() - pendingMeal.feedEndTime >= MEAL_MEASURE_DELAY)
  {
    float trayAfter = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();

    float consumed = pendingMeal.trayBefore + pendingMeal.dispensed - trayAfter;

    if (consumed < 0)
      consumed = 0;

    Serial.printf("[MEAL] Measure done. Before: %.1fg, dispensed: %.1fg, after: %.1fg, consumed: %.1fg\n",
                  pendingMeal.trayBefore,
                  pendingMeal.dispensed,
                  trayAfter,
                  consumed);

    if (online)
    {
      firebase.sendMealConsumed(
          pendingMeal.mealId,
          consumed,
          trayAfter,
          pendingMeal.hour,
          pendingMeal.minute);
    }

    pendingMeal.active = false;
  }

  // ── Đồng bộ lịch + khẩu phần (mỗi 10 giây) ──
  if (!feeder.isFeeding() && online && millis() - lastScheduleSync >= SCHEDULE_INTERVAL)
  {
    lastScheduleSync = millis();

    int h0 = -1, m0 = -1, h1 = -1, m1 = -1;
    firebase.getSchedule(h0, m0, h1, m1);
    if (h0 >= 0)
      schedule.setTime(0, h0, m0);
    if (h1 >= 0)
      schedule.setTime(1, h1, m1);

    float g = firebase.getGramsPerMeal();

    if (isValidGramsPerFeed(g))
    {
      saveGramsPerMeal(g);
    }
    else if (g > 0)
    {
      Serial.printf("[CONFIG] Firebase gramsPerMeal out of range: %.1fg\n", g);
    }
  }

  // ── Đọc cân hộp + khay (mỗi 3 giây) ──
  if (!feeder.isFeeding() && millis() - lastWeightCheck >= WEIGHT_INTERVAL)
  {
    lastWeightCheck = millis();

    if (hopperSensor.isReady())
    {
      float w;

      if (feeder.isFeeding())
      {
        // Khi đang xả, cân đã được đọc nhanh ở đầu loop()
        w = hopperSensor.getWeight();
      }
      else
      {
        // Bình thường thì đọc 5 mẫu cho ổn định
        w = hopperSensor.measure();
      }
      if (online)
      {
        firebase.sendHopperWeight(w);

        FoodState fs = hopperSensor.getFoodState();
        if (fs == FOOD_PRESENT)
          firebase.sendFoodState("PRESENT");
        else if (fs == FOOD_EMPTY)
          firebase.sendFoodState("EMPTY");
        else
          firebase.sendFoodState("UNKNOWN");
      }
    }

    // Đọc cân khay realtime
    if (traySensor.isReady())
    {
      float tw = traySensor.measure();
      if (online)
      {
        firebase.sendTrayWeight(tw);
      }
      Serial.printf("[TRAY] Weight: %.1fg\n", tw);
    }
    else
    {
      Serial.println("[TRAY] HX711 not ready");
    }
  }

  // ── Manual feed từ Firebase ──
  if (!feeder.isFeeding() && online && millis() - lastCommandCheck >= COMMAND_INTERVAL)
  {
    lastCommandCheck = millis();
    float manualGrams = -1;

    if (firebase.getManualFeedCommand(manualGrams))
    {
      Serial.printf("[FEED] Manual feed from Firebase: %.1fg\n", manualGrams);

      if (feeder.isFeeding())
      {
        Serial.println("[MANUAL] Feeder busy, command rejected");
        firebase.setManualFeedStatus("error_busy");
      }
      else if (!isValidGramsPerFeed(manualGrams))
      {
        Serial.printf("[MANUAL] Invalid manual grams: %.1fg\n", manualGrams);
        firebase.setManualFeedStatus("error_invalid_grams");
      }
      else
      {
        firebase.setManualFeedStatus("accepted");

        trayBeforeMeal = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();
        float currentW = hopperSensor.isReady() ? hopperSensor.measure() : hopperSensor.getWeight();

        mealHour = h;
        mealMinute = m;
        makeMealId(now, "manual");

        bool useWeightBased = currentW > 0;

        currentFeedIsWeightBased = useWeightBased;
        currentFeedTargetGrams = manualGrams;
        currentFeedFromManual = true;

        if (online)
        {
          firebase.setManualFeedStatus(useWeightBased ? "feeding" : "fallback_feeding");
          firebase.sendLastFed(h, m);
          firebase.sendIsFeeding(true);
        }

        if (useWeightBased)
        {
          feeder.feedAmount(manualGrams, currentW);
        }
        else
        {
          feeder.feedOnce();
        }
      }
    }
  }
  // ── Nút bấm vật lý ──
  static bool lastButtonState = HIGH;
  bool buttonState = digitalRead(BUTTON_PIN);

  if (lastButtonState == HIGH && buttonState == LOW)
  {
    if (feeder.isFeeding())
    {
      Serial.println("[BUTTON] Feeder busy, button ignored");
    }
    else
    {
      trayBeforeMeal = traySensor.isReady() ? traySensor.measure() : traySensor.getWeight();

      mealHour = h;
      mealMinute = m;
      makeMealId(now, "button");

      currentFeedIsWeightBased = false;
      currentFeedTargetGrams = 0;
      currentFeedFromManual = false;

      if (online)
      {
        firebase.sendLastFed(h, m);
        firebase.sendIsFeeding(true);
      }
      feeder.feedOnce();
    }
  }
  lastButtonState = buttonState;
}
