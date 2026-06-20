#ifndef FEED_SCHEDULE_H
#define FEED_SCHEDULE_H

#include <Arduino.h>
#include <Preferences.h>

/*
   FeedSchedule
   ------------------------------
   Quản lý lịch cho ăn (giờ / phút).
   Chỉ xử lý LOGIC thời gian,
   không điều khiển servo.
*/

#define MAX_FEEDS 2 // Số lần ăn mỗi ngày

/* Cấu trúc lưu 1 mốc thời gian cho ăn */
struct FeedTime
{
    int hour;      // 0–23
    int minute;    // 0–59
    bool fedToday; // Đã cho ăn trong ngày hiện tại chưa
};

class FeedSchedule
{
public:
    /* Khởi tạo lịch cho ăn */
    FeedSchedule();

    /* Load lịch từ bộ nhớ NVS */
    void load();

    /* Lưu lịch vào bộ nhớ NVS */
    void save();

    /* Đặt giờ cho ăn (theo index) */
    void setTime(uint8_t index, int h, int m);

    /*
       Kiểm tra có đến giờ cho ăn hay chưa

       h, m : thời gian hiện tại
       return:
       - true  : đúng giờ và CHƯA cho ăn
       - false : chưa tới giờ hoặc đã cho ăn
    */
    bool isTimeToFeed(uint8_t index, int h, int m);

    /* Reset trạng thái khi sang ngày mới */
    void resetDaily(int currentDay);

private:
    Preferences prefs;         // NVS (lưu giờ cho ăn)
    FeedTime feeds[MAX_FEEDS]; // Danh sách lịch cho ăn
    int lastDay;               // Ngày cuối cùng đã reset
};

#endif
