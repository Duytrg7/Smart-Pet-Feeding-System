#include "FeedSchedule.h"
#include <Preferences.h>

/*
   Constructor
   - Khởi tạo lịch trống
   - Đánh dấu chưa cho ăn trong ngày
*/
FeedSchedule::FeedSchedule()
{
    for (uint8_t i = 0; i < MAX_FEEDS; i++)
    {
        feeds[i].hour = -1;        // Chưa đặt giờ
        feeds[i].minute = -1;      // Chưa đặt phút
        feeds[i].fedToday = false; // Chưa cho ăn trong ngày
    }
    lastDay = -1; // Chưa có ngày hợp lệ
}

/*
   load()
   - Đọc lịch cho ăn từ NVS
   - Kiểm tra dữ liệu hợp lệ
*/
void FeedSchedule::load()
{
    prefs.begin("feed", true); // true = read-only

    for (uint8_t i = 0; i < MAX_FEEDS; i++)
    {
        String keyH = "h" + String(i);
        String keyM = "m" + String(i);

        feeds[i].hour = prefs.getInt(keyH.c_str(), -1);
        feeds[i].minute = prefs.getInt(keyM.c_str(), -1);

        // Dữ liệu không hợp lệ → reset
        if (feeds[i].hour > 23 || feeds[i].minute > 59)
        {
            feeds[i].hour = -1;
            feeds[i].minute = -1;
        }

        feeds[i].fedToday = false;
    }

    prefs.end();
}

/*
   save()
   - Lưu lịch cho ăn vào NVS
*/
void FeedSchedule::save()
{
    prefs.begin("feed", false); // false = read/write

    for (uint8_t i = 0; i < MAX_FEEDS; i++)
    {
        String keyH = "h" + String(i);
        String keyM = "m" + String(i);

        prefs.putInt(keyH.c_str(), feeds[i].hour);
        prefs.putInt(keyM.c_str(), feeds[i].minute);
    }

    prefs.end();
}

/*
   setTime()
   - Đặt giờ cho ăn theo index
   - Lưu ngay vào NVS
*/
void FeedSchedule::setTime(uint8_t index, int h, int m)
{
    if (index >= MAX_FEEDS)
        return;

    // Kiểm tra giờ/phút hợp lệ
    if (h < 0 || h > 23 || m < 0 || m > 59)
        return;

    // Nếu lịch không đổi thì không làm gì
    // Tránh reset fedToday và tránh ghi NVS liên tục
    if (feeds[index].hour == h && feeds[index].minute == m)
        return;

    feeds[index].hour = h;
    feeds[index].minute = m;
    feeds[index].fedToday = false;

    save();

    Serial.printf("[SCHEDULE] Feed %d updated: %02d:%02d\n", index, h, m);
}

/*
   isTimeToFeed()
   - Kiểm tra đúng giờ cho ăn
   - Chỉ trả true 1 lần mỗi ngày
*/
bool FeedSchedule::isTimeToFeed(uint8_t index, int h, int m)
{
    if (index >= MAX_FEEDS)
        return false;

    FeedTime &f = feeds[index];

    if (f.hour < 0)
        return false;

    if (!f.fedToday && h == f.hour && m == f.minute)
    {
        f.fedToday = true;
        return true;
    }

    return false;
}

/*
   resetDaily()
   - Reset trạng thái khi sang ngày mới
*/
void FeedSchedule::resetDaily(int currentDay)
{
    if (currentDay != lastDay)
    {
        for (uint8_t i = 0; i < MAX_FEEDS; i++)
        {
            feeds[i].fedToday = false;
        }
        lastDay = currentDay;
    }
}
