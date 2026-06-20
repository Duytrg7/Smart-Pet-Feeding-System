#ifndef TIME_SERVICE_H
#define TIME_SERVICE_H
// Header guard: tránh include lặp nhiều lần

#include <RtcDS1302.h>
#include <ThreeWire.h>

/*
   TimeService
   ---------------------------
   Quản lý và cung cấp thời gian cho hệ thống.

   - Nguồn chính : NTP (Internet)
   - Lưu trữ     : RTC DS1302

   Chỉ xử lý THỜI GIAN,
   không phụ thuộc Blynk hay logic cho ăn.
*/
class TimeService
{
public:
    /*
       Khởi tạo với các chân RTC DS1302
       dat : DATA, clk : CLOCK, rst : RESET
    */
    TimeService(int dat, int clk, int rst);

    /* Khởi động RTC (gọi trong setup) */
    void begin();

    /* Cấu hình NTP (gọi 1 lần) */
    void setupNTP();

    /*
       Đồng bộ thời gian từ Internet → RTC
       Gọi lặp trong loop cho đến khi sync xong
    */
    void syncFromInternet();

    /* Lấy thời gian hiện tại từ RTC */
    RtcDateTime now();

private:
    // Giao tiếp 3 dây với RTC DS1302
    ThreeWire wire;

    // Đối tượng RTC DS1302
    RtcDS1302<ThreeWire> rtc;

    // Cờ đánh dấu đã đồng bộ thời gian hay chưa
    bool synced;
};

#endif
