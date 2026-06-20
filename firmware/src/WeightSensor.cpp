#include "WeightSensor.h"
#include <math.h>

WeightSensor::WeightSensor(const char *name, int dout, int sck, float calibFactor)
    : name(name), doutPin(dout), sckPin(sck), calibFactor(calibFactor),
      weight(0.0f), offset(0),
      foodState(FOOD_UNKNOWN), emptyWarned(false)
{
}

void WeightSensor::begin(bool forceTare)
{
    Serial.println("HX711 init...");
    Serial.print("[SCALE] Name: ");
    Serial.println(name);
    Serial.print("[SCALE] DOUT: ");
    Serial.print(doutPin);
    Serial.print(" | SCK: ");
    Serial.print(sckPin);
    Serial.print(" | Calib: ");
    Serial.println(calibFactor, 3);

    scale.begin(doutPin, sckPin, 128);
    scale.set_scale(calibFactor);

    bool hasOffset = loadOffset();

    if (forceTare || !hasOffset)
    {
        Serial.print("[SCALE] ");
        Serial.print(name);
        Serial.println(": No offset or force tare. Make sure scale is EMPTY!");

        tare(15);
    }
    else
    {
        scale.set_offset(offset);

        Serial.print("[SCALE] ");
        Serial.print(name);
        Serial.print(": Loaded offset = ");
        Serial.println(offset);
    }

    Serial.print("[SCALE] ");
    Serial.print(name);
    Serial.print(" raw average: ");
    Serial.println(scale.read_average(10));
}

bool WeightSensor::isReady()
{
    return scale.is_ready();
}

void WeightSensor::updateFoodState(float w)
{
    switch (foodState)
    {
    case FOOD_UNKNOWN:
        if (w <= FOOD_EMPTY_THRESHOLD)
        {
            foodState = FOOD_EMPTY;
            emptyWarned = false;
            Serial.printf("[FOOD][%s] EMPTY (init)\n", name);
        }
        else if (w > FOOD_PRESENT_THRESHOLD)
        {
            foodState = FOOD_PRESENT;
            emptyWarned = false;
            Serial.printf("[FOOD][%s] PRESENT (init)\n", name);
        }
        break;
    case FOOD_PRESENT:
        if (w <= FOOD_EMPTY_THRESHOLD)
        {
            foodState = FOOD_EMPTY;
            emptyWarned = false;
            Serial.printf("[FOOD][%s] EMPTY\n", name);
        }
        break;
    case FOOD_EMPTY:
        if (w > FOOD_PRESENT_THRESHOLD)
        {
            foodState = FOOD_PRESENT;
            emptyWarned = false;
            Serial.printf("[FOOD][%s] REFILLED\n", name);
        }
        break;
    }
}

float WeightSensor::measure()
{
    if (!scale.is_ready())
        return weight;
    float newWeight = scale.get_units(5);
    if (newWeight < 0)
        newWeight = 0.0f;
    weight = newWeight;
    updateFoodState(weight);
    return weight;
}

float WeightSensor::measureFast()
{
    if (!scale.is_ready())
        return weight;

    float newWeight = scale.get_units(1); // đọc 1 mẫu cho nhanh

    if (newWeight < 0)
        newWeight = 0.0f;

    weight = newWeight;
    updateFoodState(weight);

    return weight;
}

float WeightSensor::measureFastFiltered(float maxStepGrams)
{
    if (!scale.is_ready())
        return weight;

    float newWeight = scale.get_units(1);

    if (newWeight < 0)
        newWeight = 0.0f;

    // Chặn bước nhảy quá lớn do nhiễu/rung cơ khí
    if (weight > 0.0f && maxStepGrams > 0.0f)
    {
        float diff = newWeight - weight;

        if (fabsf(diff) > maxStepGrams)
        {
            if (diff > 0)
                newWeight = weight + maxStepGrams;
            else
                newWeight = weight - maxStepGrams;
        }
    }

    // Làm mượt nhẹ, vẫn ưu tiên giá trị mới để phản ứng nhanh khi đang xả
    weight = weight * 0.55f + newWeight * 0.45f;

    if (weight < 0)
        weight = 0.0f;

    updateFoodState(weight);

    return weight;
}

float WeightSensor::getWeight()
{
    return weight;
}

bool WeightSensor::isFoodEmpty()
{
    if (foodState == FOOD_EMPTY && !emptyWarned)
    {
        emptyWarned = true;
        return true;
    }
    return false;
}

FoodState WeightSensor::getFoodState()
{
    return foodState;
}

bool WeightSensor::loadOffset()
{
    prefs.begin(name, true);

    if (!prefs.isKey("offset"))
    {
        prefs.end();
        return false;
    }

    offset = prefs.getLong("offset", 0);
    prefs.end();

    return true;
}

void WeightSensor::saveOffset()
{
    prefs.begin(name, false);
    prefs.putLong("offset", offset);
    prefs.end();
}

void WeightSensor::tare(uint8_t samples)
{
    if (!scale.is_ready())
    {
        Serial.print("[SCALE] ");
        Serial.print(name);
        Serial.println(": HX711 not ready, tare skipped");
        return;
    }

    Serial.print("[SCALE] ");
    Serial.print(name);
    Serial.println(": Taring...");

    scale.tare(samples);
    offset = scale.get_offset();
    saveOffset();

    weight = 0.0f;
    foodState = FOOD_UNKNOWN;
    emptyWarned = false;

    Serial.print("[SCALE] ");
    Serial.print(name);
    Serial.print(": New offset saved = ");
    Serial.println(offset);
}

long WeightSensor::getOffset()
{
    return offset;
}

long WeightSensor::getRawAverage(uint8_t samples)
{
    if (!scale.is_ready())
        return 0;

    return scale.read_average(samples);
}