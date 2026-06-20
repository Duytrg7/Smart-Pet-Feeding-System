#include "FirebaseService.h"
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <math.h>

void FirebaseService::begin()
{
    config.api_key = FIREBASE_API_KEY;
    config.database_url = FIREBASE_DATABASE_URL;
    config.token_status_callback = tokenStatusCallback;

    Firebase.signUp(&config, &auth, "", "");
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    Serial.println("[FIREBASE] Initialized");
}

bool FirebaseService::isReady()
{
    return Firebase.ready();
}

void FirebaseService::update()
{
    // Firebase tự duy trì kết nối
}

/* ════════════════════════════════════════
   GHI TRẠNG THÁI
════════════════════════════════════════ */

void FirebaseService::sendHopperWeight(float w)
{
    if (!Firebase.ready())
        return;
    Firebase.RTDB.setFloat(&fbdo, "petfeeder/status/hopperWeight", w);
}

void FirebaseService::sendFoodState(const char *state)
{
    if (!Firebase.ready())
        return;
    Firebase.RTDB.setString(&fbdo, "petfeeder/status/foodState", state);
}

void FirebaseService::sendLastFed(int h, int m)
{
    if (!Firebase.ready())
        return;
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    Firebase.RTDB.setString(&fbdo, "petfeeder/status/lastFed", buf);
}

void FirebaseService::sendIsFeeding(bool feeding)
{
    if (!Firebase.ready())
        return;
    Firebase.RTDB.setBool(&fbdo, "petfeeder/status/isFeeding", feeding);
}

/*
   sendDispensed()
   Ghi 2 nơi:
   1. petfeeder/status/lastDispensed  → web đọc realtime
   2. petfeeder/log/<timestamp>       → lịch sử cho ăn
*/
void FirebaseService::sendDispensed(const char *mealId, float targetG, float actualG, float trayBefore, int h, int m)
{
    if (!Firebase.ready())
        return;

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);

    // ── 1. Realtime status cho web hiện tại ──
    FirebaseJson statusJson;
    statusJson.set("time", timeBuf);
    statusJson.set("target", targetG);
    statusJson.set("actual", actualG);
    statusJson.set("trayBefore", trayBefore);

    if (!Firebase.RTDB.setJSON(&fbdo, "petfeeder/status/lastDispensed", &statusJson))
    {
        Serial.print("[FIREBASE] lastDispensed error: ");
        Serial.println(fbdo.errorReason());
    }

    // ── 2. Log đầy đủ cho lịch sử bữa ăn ──
    FirebaseJson logJson;
    logJson.set("mealId", mealId);
    logJson.set("time", timeBuf);
    logJson.set("target", targetG);
    logJson.set("actualDispensed", actualG);
    logJson.set("trayBefore", trayBefore);
    logJson.set("status", "dispensed");

    String path = "petfeeder/mealLogs/";
    path += mealId;

    if (Firebase.RTDB.setJSON(&fbdo2, path.c_str(), &logJson))
    {
        Serial.printf("[FIREBASE] Meal log created: %s | %.1fg / %.1fg\n",
                      mealId, actualG, targetG);
    }
    else
    {
        Serial.print("[FIREBASE] sendDispensed error: ");
        Serial.println(fbdo2.errorReason());
    }
}

/* ════════════════════════════════════════
   ĐỌC LỆNH / CẤU HÌNH
════════════════════════════════════════ */

bool FirebaseService::getManualFeedCommand(float &grams)
{
    grams = -1;

    if (!Firebase.ready())
        return false;

    FirebaseJson json;
    FirebaseJsonData result;

    if (!Firebase.RTDB.getJSON(&fbdo, "petfeeder/command/feedNow", &json))
        return false;

    // Đọc status
    if (!json.get(result, "status"))
        return false;

    String status = result.stringValue;

    // Chỉ xử lý command đang pending
    if (status != "pending")
        return false;

    // Đọc grams
    if (!json.get(result, "grams"))
    {
        Serial.println("[FIREBASE] FeedNow invalid: missing grams");
        Firebase.RTDB.setString(
            &fbdo,
            "petfeeder/command/feedNow/status",
            "error_invalid_grams");
        return false;
    }

    grams = result.floatValue;

    // Validate grams
    if (isnan(grams) || grams <= 0 || grams > 500)
    {
        Serial.printf("[FIREBASE] FeedNow invalid grams: %.1f\n", grams);
        Firebase.RTDB.setString(
            &fbdo,
            "petfeeder/command/feedNow/status",
            "error_invalid_grams");
        return false;
    }

    Firebase.RTDB.setString(
        &fbdo,
        "petfeeder/command/feedNow/status",
        "processing");

    Serial.printf("[FIREBASE] FeedNow command: %.1fg\n", grams);
    return true;
}

void FirebaseService::setManualFeedStatus(const char *status)
{
    if (!Firebase.ready())
        return;

    if (Firebase.RTDB.setString(&fbdo, "petfeeder/command/feedNow/status", status))
    {
        Serial.print("[FIREBASE] Manual feed status: ");
        Serial.println(status);
    }
    else
    {
        Serial.print("[FIREBASE] Manual feed status error: ");
        Serial.println(fbdo.errorReason());
    }
}

void FirebaseService::getSchedule(int &h0, int &m0, int &h1, int &m1)
{
    if (!Firebase.ready())
        return;

    FirebaseJson json;
    FirebaseJsonData result;

    if (!Firebase.RTDB.getJSON(&fbdo, "petfeeder/schedule", &json))
        return;

    if (json.get(result, "feed0/hour"))
        h0 = result.intValue;

    if (json.get(result, "feed0/minute"))
        m0 = result.intValue;

    if (json.get(result, "feed1/hour"))
        h1 = result.intValue;

    if (json.get(result, "feed1/minute"))
        m1 = result.intValue;
}

/*
   getGramsPerMeal()
   path: petfeeder/config/gramsPerMeal
   - Trả về float gram
   - Trả về -1 nếu chưa cài hoặc giá trị không hợp lệ
*/
float FirebaseService::getGramsPerMeal()
{
    if (!Firebase.ready())
        return -1;

    if (Firebase.RTDB.getFloat(&fbdo, "petfeeder/config/gramsPerMeal"))
    {
        float g = fbdo.floatData();
        if (g > 0 && g < 500) // Giới hạn hợp lý: 0–500g/bữa
        {
            Serial.printf("[FIREBASE] gramsPerMeal: %.1fg\n", g);
            return g;
        }
    }
    return -1;
}

/*
   setGramsPerMeal()
   path: petfeeder/config/gramsPerMeal
*/
void FirebaseService::setGramsPerMeal(float g)
{
    if (!Firebase.ready())
        return;
    if (g <= 0 || g >= 500)
        return;
    Firebase.RTDB.setFloat(&fbdo, "petfeeder/config/gramsPerMeal", g);
    Serial.printf("[FIREBASE] Set gramsPerMeal: %.1fg\n", g);
}

void FirebaseService::sendTrayWeight(float w)
{
    if (!Firebase.ready())
        return;
    Firebase.RTDB.setFloat(&fbdo, "petfeeder/status/trayWeight", w);
}

/*
   sendMealConsumed()
   Ghi lượng thú cưng đã ăn thực tế sau khoảng thời gian theo dõi bữa ăn
   path: petfeeder/status/lastMealConsumed
*/
void FirebaseService::sendMealConsumed(const char *mealId, float consumed, float trayAfter, int h, int m)
{
    if (!Firebase.ready())
        return;

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", h, m);

    // ── 1. Realtime status cho web hiện tại ──
    FirebaseJson statusJson;
    statusJson.set("time", timeBuf);
    statusJson.set("consumed", consumed);
    statusJson.set("trayAfter", trayAfter);

    if (!Firebase.RTDB.setJSON(&fbdo, "petfeeder/status/lastMealConsumed", &statusJson))
    {
        Serial.print("[FIREBASE] lastMealConsumed error: ");
        Serial.println(fbdo.errorReason());
    }

    // ── 2. Update log bữa ăn, không ghi đè field cũ ──
    FirebaseJson updateJson;
    updateJson.set("trayAfter", trayAfter);
    updateJson.set("consumed", consumed);
    updateJson.set("consumedTime", timeBuf);
    updateJson.set("status", "completed");

    String path = "petfeeder/mealLogs/";
    path += mealId;

    if (Firebase.RTDB.updateNode(&fbdo2, path.c_str(), &updateJson))
    {
        Serial.printf("[FIREBASE] Meal completed: %s | consumed %.1fg\n",
                      mealId, consumed);
    }
    else
    {
        Serial.print("[FIREBASE] sendMealConsumed error: ");
        Serial.println(fbdo2.errorReason());
    }
}