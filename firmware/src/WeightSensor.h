#ifndef WEIGHT_SENSOR_H
#define WEIGHT_SENSOR_H

#include <Arduino.h>
#include <HX711.h>
#include <Preferences.h>

#define FOOD_EMPTY_THRESHOLD 5.0f
#define FOOD_PRESENT_THRESHOLD 50.0f

enum FoodState
{
    FOOD_UNKNOWN,
    FOOD_PRESENT,
    FOOD_EMPTY
};

class WeightSensor
{
public:
    WeightSensor(const char *name, int dout, int sck, float calibFactor = 211000.0f);

    // forceTare = true chỉ dùng khi chắc chắn cân đang rỗng
    void begin(bool forceTare = false);

    bool isReady();

    float measure();
    float measureFast();
    float measureFastFiltered(float maxStepGrams = 40.0f);
    float getWeight();

    bool isFoodEmpty();
    FoodState getFoodState();

    // Tare thủ công và lưu offset vào NVS
    void tare(uint8_t samples = 15);

    // Debug
    long getOffset();
    long getRawAverage(uint8_t samples = 10);

private:
    HX711 scale;
    Preferences prefs;

    const char *name;
    int doutPin;
    int sckPin;
    float calibFactor;
    float weight;
    long offset;

    FoodState foodState;
    bool emptyWarned;

    void updateFoodState(float w);
    bool loadOffset();
    void saveOffset();
};

#endif