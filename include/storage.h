#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

// Recipes and the fill log, stored on the ESP32 flash (LittleFS).
// Weights are kept in tenths of a grain; the JSON API uses grains.
namespace Recipes {
struct Recipe {
    uint32_t id = 0;
    String name, caliber, bullet, powder, primer, brass, oal, cbto, notes;
    int bulletWeight = 0;  // tenths of a grain
    int charge = 0;        // tenths of a grain: the fill target
    int fastRpm = 120;
    int slowRpm = 15;
    int slowAt = 20;       // tenths of a grain before the target
    uint32_t updated = 0;  // unix time, 0 if the clock was not set
};

// Loads recipes; creates `fallback` when there are none.
bool begin(const Recipe& fallback);
int count();
const Recipe& at(int index);
int activeIndex();
const Recipe& active();
int indexOf(uint32_t id);
void select(int index);
// Adds (id 0) or replaces a recipe. Returns its id, 0 on failure.
uint32_t upsert(const Recipe& recipe);
bool remove(uint32_t id);
// Saves the knob-editable values of the active recipe.
void updateActive(int charge, int fastRpm, int slowRpm, int slowAt);
// Changes whenever recipes or the active recipe change.
uint32_t revision();

void toJson(JsonObject out, const Recipe& recipe);
void fromJson(JsonObjectConst in, Recipe& recipe);
}

namespace FillLog {
constexpr const char* Path = "/log.csv";
// One line per fill: time,target,actual,result,"recipe"
void append(const String& recipe, int target, int actual, const char* result);
void clear();
}

// Unix time, or 0 when the clock has not been set yet.
uint32_t clockNow();
