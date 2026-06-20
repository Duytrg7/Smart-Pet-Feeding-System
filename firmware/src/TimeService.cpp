#include "TimeService.h"
#include <TimeLib.h>
#include <time.h>

/* ================== CẤU HÌNH NTP ================== */
static const char *NTP_SERVER = "pool.ntp.org";
static const long GMT_OFFSET = 7 * 3600; // GMT+7 (Việt Nam)
static const int DAYLIGHT_OFFSET = 0;

/*
   Constructor
   - Khởi tạo giao tiếp 3 dây cho RTC DS1302
   - Gắn ThreeWire vào đối tượng RTC
   - Đặt cờ synced = false (chưa đồng bộ Internet)
*/
TimeService::TimeService(int dat, int clk, int rst)
    : wire(dat, clk, rst), rtc(wire)
{
    synced = false;
}

/*
   Khởi động RTC DS1302
   Hàm này được gọi một lần trong setup()
*/
void TimeService::begin()
{
    rtc.Begin();
    rtc.SetIsWriteProtected(false);
    if (!rtc.GetIsRunning())
    {
        rtc.SetIsRunning(true);
    }
}

/*
   Cấu hình NTP
   - Chỉ gọi 1 lần trong setup()
*/
void TimeService::setupNTP()
{
    configTime(GMT_OFFSET, DAYLIGHT_OFFSET, NTP_SERVER);
}

/*
   Đồng bộ thời gian Internet vào RTC DS1302
   - Chỉ thực hiện một lần
   - Điều kiện year() > 2023 để chắc chắn đã có thời gian hợp lệ
   - Hàm này nên được gọi liên tục trong loop()
*/
void TimeService::syncFromInternet()
{
    if (synced)
        return;

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return; // chưa có giờ Internet
    }

    // Kiểm tra năm để đảm bảo thời gian hợp lệ
    if (timeinfo.tm_year + 1900 < 2024)
    {
        return;
    }

    rtc.SetDateTime(RtcDateTime(
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec));

    synced = true;
}

/*
   Lấy thời gian hiện tại từ RTC DS1302
   Dùng cho các module khác (FeedSchedule, debug, ...)
*/
RtcDateTime TimeService::now()
{
    return rtc.GetDateTime();
}
